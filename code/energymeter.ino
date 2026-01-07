#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include <PZEM004Tv30.h>
#include <SoftwareSerial.h>
#include <EEPROM.h>

// LCD Configuration
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Keypad Configuration
const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = {9, 8, 7, 6};
byte colPins[COLS] = {5, 4, 3, 2};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// PZEM Energy Monitor
#define PZEM_RX_PIN 12
#define PZEM_TX_PIN 11
SoftwareSerial pzemSerial(PZEM_RX_PIN, PZEM_TX_PIN);
PZEM004Tv30 pzem(pzemSerial);

// Reset switch
const int resetSwitchPin = A3;

// Low voltage LED indicator
const int lowVoltageLED = A2;

// System Modes
enum DisplayMode {
  BASIC_READINGS,
  POWER_READINGS,
  APPLIANCE_MENU,
  DOMESTIC_BILL,
  INDUSTRIAL_BILL,
  TARIFF_MENU,
  TARIFF_EDIT
};

// Tariff Structures
struct DomesticTariffBand {
  int maxKWh;
  float energyCharge;
  float fixedCharge;
};

struct IndustrialTariffBand {
  int maxKWh;
  float energyCharge;
  float fixedCharge;
};

// Default Domestic Tariff Values
#define NUM_DOMESTIC_BANDS 7
DomesticTariffBand domesticBands[NUM_DOMESTIC_BANDS] = {
  {30, 4.50, 80.00},    
  {60, 8.00, 210.00},   
  {90, 12.75, 0},       
  {120, 18.50, 400.00},  
  {180, 24.00, 1000.00},
  {300, 41.00, 1500.00},
  {999, 61.00, 2100.00} 
};

// Default Industrial Tariff Values
#define NUM_INDUSTRIAL_BANDS 2
IndustrialTariffBand industrialBands[NUM_INDUSTRIAL_BANDS] = {
  {300, 8.00, 300.00},
  {9999, 17.00, 800.00}
};

// Global Variables
DisplayMode currentMode = BASIC_READINGS;
bool isDomestic = true;
int selectedBand = 0;
int editField = 0;
int cursorPosition = 0;
unsigned long lastRefresh = 0;
const long refreshInterval = 500;

// Sensor Values
float voltage = 0;
float current = 0;
float power = 0;
float energy = 0;
float frequency = 0;
float pf = 0;
float billAmount = 0;
float apparentPower = 0;

// *** FIXED: Energy offset to preserve across power cycles ***
float energyOffset = 0;

// EEPROM Energy Storage
#define ENERGY_EEPROM_START 500
#define ENERGY_EEPROM_END 900
int energyEEPROMPointer = ENERGY_EEPROM_START;
unsigned long lastEnergySave = 0;
const unsigned long energySaveInterval = 30000;

// Function to save energy to EEPROM
void saveEnergyToEEPROM(float energyValue) {
  // Save total accumulated energy at fixed location
  EEPROM.put(ENERGY_EEPROM_START, energyValue);
  
  // Save pointer position
  EEPROM.put(ENERGY_EEPROM_START + sizeof(float), energyEEPROMPointer);
  
  // Move to next circular buffer position
  energyEEPROMPointer += sizeof(float);
  if (energyEEPROMPointer + sizeof(float) > ENERGY_EEPROM_END) {
    energyEEPROMPointer = ENERGY_EEPROM_START + sizeof(float) + sizeof(int);
  }
}

// Function to reset energy values
void resetEnergyValues() {
  energyOffset = 0;
  energy = 0;
  pzem.resetEnergy();
  energyEEPROMPointer = ENERGY_EEPROM_START + sizeof(float) + sizeof(int);
  billAmount = 0;
  
  // Clear EEPROM energy values
  EEPROM.put(ENERGY_EEPROM_START, 0.0f);
  EEPROM.put(ENERGY_EEPROM_START + sizeof(float), energyEEPROMPointer);

  lcd.clear();
  lcd.print("Energy Reset!");
  delay(1000);
}

// Function to read VIN voltage internally
float readVinVoltage() {
  ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  delay(2);
  ADCSRA |= _BV(ADSC);
  while (bit_is_set(ADCSRA, ADSC));
  
  uint8_t low = ADCL;
  uint8_t high = ADCH;
  long result = (high << 8) | low;
  
  float vcc = 1125300L / result;
  
  return vcc / 1000.0;
}

// Function to check and indicate low voltage
void checkLowVoltage() {
  float vinVoltage = readVinVoltage();
  
  if (vinVoltage < 4.3) {
    digitalWrite(lowVoltageLED, HIGH);
  } else {
    digitalWrite(lowVoltageLED, LOW);
  }
}

// Setup
void setup() {
  Serial.begin(9600);
  Wire.begin();
  lcd.init();
  lcd.backlight();
  pinMode(lowVoltageLED, OUTPUT); 
  
  // **UNCOMMENT THESE 2 LINES TO RESET EEPROM, THEN COMMENT AGAIN**
  // EEPROM.put(0, (byte)0);  
  // delay(100);
  
  lcd.clear();
  lcd.print("Energy Meter");
  lcd.setCursor(0, 1);
  lcd.print("Initializing...");
  delay(1500);
  
  pinMode(resetSwitchPin, INPUT_PULLUP);
  
  pzemSerial.begin(9600);
  loadFromEEPROM();
  
  // *** FIXED: Reset PZEM counter after loading offset ***
  pzem.resetEnergy();
  
  lcd.clear();
}

// Main loop
void loop() {
  checkLowVoltage();
  
  if (digitalRead(resetSwitchPin) == LOW) {
    resetEnergyValues();
    delay(500);
  }

  char key = keypad.getKey();
  if (key) handleKeyInput(key);

  if (millis() - lastRefresh >= refreshInterval) {
    updateSensorData();
    if (currentMode == DOMESTIC_BILL) calculateDomesticBill();
    if (currentMode == INDUSTRIAL_BILL) calculateIndustrialBill();
    updateDisplay();
    lastRefresh = millis();
  }

  if (millis() - lastEnergySave >= energySaveInterval) {
    saveEnergyToEEPROM(energy);
    lastEnergySave = millis();
  }
}

// Keypad Input Handling
void handleKeyInput(char key) {
  switch(currentMode) {
    case BASIC_READINGS:
      if (key == 'A') currentMode = POWER_READINGS;
      else if (key == 'B') currentMode = APPLIANCE_MENU;
      break;
      
    case POWER_READINGS:
      if (key == 'A') currentMode = BASIC_READINGS;
      break;
      
    case APPLIANCE_MENU:
      if (key == '1') { 
        isDomestic = true; 
        currentMode = DOMESTIC_BILL; 
      }
      else if (key == '2') { 
        isDomestic = false; 
        currentMode = INDUSTRIAL_BILL; 
      }
      else if (key == '#') currentMode = BASIC_READINGS;
      break;
      
    case DOMESTIC_BILL:
      if (key == 'A') currentMode = TARIFF_MENU;
      else if (key == '#') currentMode = BASIC_READINGS;
      break;
      
    case INDUSTRIAL_BILL:
      if (key == 'A') currentMode = TARIFF_MENU;
      else if (key == '#') currentMode = BASIC_READINGS;
      break;
      
    case TARIFF_MENU:
      if (isDomestic) {
        if (key >= '1' && key <= '7') {
          selectedBand = key - '1';
          editField = 0;   
          cursorPosition = 0;
          currentMode = TARIFF_EDIT;
        }
        else if (key == '#') currentMode = DOMESTIC_BILL;
      } else {
        if (key == '1' || key == '2') {
          selectedBand = key - '1';
          editField = 0;
          cursorPosition = 0;
          currentMode = TARIFF_EDIT;
        }
        else if (key == '#') currentMode = INDUSTRIAL_BILL;
      }
      break;
      
    case TARIFF_EDIT:
      if (key == 'D') {
        cursorPosition = (cursorPosition + 1) % getMaxCursorPositions();
      }
      else if (key >= '0' && key <= '9') {
        updateSelectedDigit(key - '0');
      }
      else if (key == '*') { 
        saveToEEPROM(); 
        returnToBillScreen(); 
      }
      else if (key == 'C') {
        editField = (editField + 1) % 2;
        cursorPosition = 0;
      }
      else if (key == '#') {
        returnToBillScreen();
      }
      break;
  }
}

void returnToBillScreen() {
  if (isDomestic) currentMode = DOMESTIC_BILL;
  else currentMode = INDUSTRIAL_BILL;
}

int getMaxCursorPositions() {
  return (editField == 0) ? 4 : 5;
}

void updateSelectedDigit(int digit) {
  float* value;
  int decimals;

  if (isDomestic) {  
    DomesticTariffBand* band = &domesticBands[selectedBand];
    if (editField == 0) {
      value = &band->energyCharge;
      decimals = 2;
    } else {
      value = &band->fixedCharge;
      decimals = 1;
    }
  } else {
    IndustrialTariffBand* band = &industrialBands[selectedBand];
    if (editField == 0) {
      value = &band->energyCharge;
      decimals = 2;
    } else {
      value = &band->fixedCharge;
      decimals = 1;
    }
  }

  editFloatValue(value, digit, decimals);
}

void editFloatValue(float* value, int digit, int decimalPlaces) {
  int scale = 1;
  for (int i = 0; i < decimalPlaces; i++) scale *= 10;

  long scaledValue = (long)(*value * scale + 0.5f);

  int totalDigits = getMaxCursorPositions();
  int posFromRight = totalDigits - 1 - cursorPosition;

  long power = 1;
  for (int i = 0; i < posFromRight; i++) power *= 10;

  scaledValue -= (scaledValue / power % 10) * power;
  scaledValue += digit * power;

  *value = (float)scaledValue / scale;
}

// Sensor Updates
void updateSensorData() {
  voltage = pzem.voltage();
  if (isnan(voltage) || voltage < 0.1) {
    voltage = current = power = pf = apparentPower = 0;
  } else {
    current = pzem.current();
    power = pzem.power();
    pf = pzem.pf();
    apparentPower = voltage * current;
  }
  
  // *** FIXED: Add offset to PZEM reading for total accumulated energy ***
  float pzemEnergy = pzem.energy();
  if (isnan(pzemEnergy)) pzemEnergy = 0;
  energy = energyOffset + pzemEnergy;
  
  frequency = pzem.frequency();

  if (isnan(current)) current = 0;
  if (isnan(power)) power = 0;
  if (isnan(pf)) pf = 0;
  if (isnan(frequency)) frequency = 0;
}

// Bill Calculations
void calculateDomesticBill() {
  float monthlyKWh = energy;  
  billAmount = 0;
  float fixedCharge = 0;

  if (monthlyKWh <= 60) {
    if (monthlyKWh <= 30) {
      billAmount = monthlyKWh * domesticBands[0].energyCharge;
      fixedCharge = domesticBands[0].fixedCharge;
    } else {
      billAmount = 30 * domesticBands[0].energyCharge;
      billAmount += (monthlyKWh - 30) * domesticBands[1].energyCharge;
      fixedCharge = domesticBands[1].fixedCharge;
    }
  } else {
    billAmount += 60 * domesticBands[2].energyCharge;
    if (monthlyKWh > 60) {
      float kWh = min(monthlyKWh - 60, 30.0f);
      billAmount += kWh * domesticBands[3].energyCharge;
      fixedCharge = domesticBands[3].fixedCharge;
    }
    if (monthlyKWh > 90) {
      float kWh = min(monthlyKWh - 90, 30.0f);
      billAmount += kWh * domesticBands[4].energyCharge;
      fixedCharge = domesticBands[4].fixedCharge;
    }
    if (monthlyKWh > 120) {
      float kWh = min(monthlyKWh - 120, 60.0f);
      billAmount += kWh * domesticBands[5].energyCharge;
      fixedCharge = domesticBands[5].fixedCharge;
    }
    if (monthlyKWh > 180) {
      float kWh = monthlyKWh - 180;
      billAmount += kWh * domesticBands[6].energyCharge;
      fixedCharge = domesticBands[6].fixedCharge;
    }
  }

  billAmount += fixedCharge;
}

void calculateIndustrialBill() {
  float monthlyKWh = energy;
  billAmount = 0;
  float fixedCharge = 0;

  if (monthlyKWh <= industrialBands[0].maxKWh) {
    billAmount = monthlyKWh * industrialBands[0].energyCharge;
    fixedCharge = industrialBands[0].fixedCharge;
  } else {
    billAmount = industrialBands[0].maxKWh * industrialBands[0].energyCharge;
    
    float remainingKWh = monthlyKWh - industrialBands[0].maxKWh;
    billAmount += remainingKWh * industrialBands[1].energyCharge;
    fixedCharge = industrialBands[1].fixedCharge;
  }

  billAmount += fixedCharge;
}

// Display Functions
void updateDisplay() {
  lcd.clear();
  switch(currentMode) {
    case BASIC_READINGS: showBasicReadings(); break;
    case POWER_READINGS: showPowerReadings(); break;
    case APPLIANCE_MENU: showApplianceMenu(); break;
    case DOMESTIC_BILL: showDomesticBill(); break;
    case INDUSTRIAL_BILL: showIndustrialBill(); break;
    case TARIFF_MENU: showTariffMenu(); break;
    case TARIFF_EDIT: showTariffEditor(); break;
  }
}

void showBasicReadings() {
  lcd.setCursor(0, 0);
  lcd.print("V:"); lcd.print(voltage, 1); lcd.print("V");
  lcd.setCursor(8, 0);
  lcd.print("I:"); lcd.print(current, 2); lcd.print("A");
  
  lcd.setCursor(0, 1);
  lcd.print("P:"); lcd.print(power, 1); lcd.print("W");
  lcd.setCursor(8, 1);
  lcd.print("E:"); lcd.print(energy, 1); lcd.print("kWh");
}

void showPowerReadings() {
  float reactivePower = sqrt(sq(apparentPower) - sq(power));
  lcd.setCursor(0, 0);
  lcd.print("PF:"); lcd.print(pf, 2);
  lcd.setCursor(8, 0);
  lcd.print("S:"); lcd.print(apparentPower, 1); lcd.print("VA");
  
  lcd.setCursor(0, 1);
  lcd.print("Q:"); lcd.print(reactivePower, 1); lcd.print("var");
  lcd.setCursor(8, 1);
  lcd.print("F:"); lcd.print(frequency, 1);
}

void showApplianceMenu() {
  lcd.setCursor(0, 0);
  lcd.print("Appliance Type");
  lcd.setCursor(0, 1);
  lcd.print("1.Dom 2.Ind ");
}

void showDomesticBill() {
  lcd.setCursor(0, 0);
  lcd.print("Bill Category");
  lcd.setCursor(0, 1);
  lcd.print("Rs."); lcd.print(billAmount, 2);
}

void showIndustrialBill() {
  lcd.setCursor(0, 0);
  lcd.print("Industrial Bill");
  lcd.setCursor(0, 1);
  lcd.print("Rs."); lcd.print(billAmount, 2);
}

void showTariffMenu() {
  lcd.setCursor(0, 0);
  if (isDomestic) {
    lcd.print("Domestic Tariff");
    lcd.setCursor(0, 1);
    lcd.print("1-7:Band");
  } else {
    lcd.print("Industrial Tariff");
    lcd.setCursor(0, 1);
    lcd.print("1-2:Band");
  }
}

void showTariffEditor() {
  lcd.setCursor(0, 0);
  
  if (isDomestic) {
    if (selectedBand < NUM_DOMESTIC_BANDS - 1) {
      lcd.print(selectedBand == 0 ? "0-" : String(domesticBands[selectedBand-1].maxKWh) + "-");
      lcd.print(domesticBands[selectedBand].maxKWh); lcd.print("kWh");
    } else {
      lcd.print(">"); lcd.print(domesticBands[NUM_DOMESTIC_BANDS-2].maxKWh); lcd.print("kWh");
    }
    
    lcd.setCursor(0, 1);
    if (editField == 0) {
      lcd.print("Rs/kWh:"); lcd.print(domesticBands[selectedBand].energyCharge, 2);
      lcd.setCursor(7 + cursorPosition + (cursorPosition > 1 ? 1 : 0), 1);
    } else {
      lcd.print("Fixed:"); lcd.print(domesticBands[selectedBand].fixedCharge, 1);
      lcd.setCursor(6 + cursorPosition + (cursorPosition > 3 ? 1 : 0), 1);
    }
  } else {
    if (selectedBand == 0) {
      lcd.print("0-"); lcd.print(industrialBands[0].maxKWh); lcd.print("kWh");
    } else {
      lcd.print(">"); lcd.print(industrialBands[0].maxKWh); lcd.print("kWh");
    }
    
    lcd.setCursor(0, 1);
    if (editField == 0) {
      lcd.print("Rs/kWh:"); lcd.print(industrialBands[selectedBand].energyCharge, 2);
      lcd.setCursor(7 + cursorPosition + (cursorPosition > 1 ? 1 : 0), 1);
    } else {
      lcd.print("Fixed:"); lcd.print(industrialBands[selectedBand].fixedCharge, 1);
      lcd.setCursor(6 + cursorPosition + (cursorPosition > 3 ? 1 : 0), 1);
    }
  }
  
  lcd.cursor();
}

// EEPROM Functions
void loadFromEEPROM() {
  byte initialized;
  EEPROM.get(0, initialized);
  
  if (initialized == 123) {
    int address = 1;
    
    // Load domestic bands
    for (int i = 0; i < NUM_DOMESTIC_BANDS; i++) {
      EEPROM.get(address, domesticBands[i]);
      address += sizeof(DomesticTariffBand);
    }
    
    // Load industrial bands
    for (int i = 0; i < NUM_INDUSTRIAL_BANDS; i++) {
      EEPROM.get(address, industrialBands[i]);
      address += sizeof(IndustrialTariffBand);
    }
    
    // *** FIXED: Load energy offset ***
    EEPROM.get(ENERGY_EEPROM_START, energyOffset);
    if (isnan(energyOffset) || energyOffset < 0) {
      energyOffset = 0;
    }
    
    // *** FIXED: Load and restore circular buffer pointer ***
    EEPROM.get(ENERGY_EEPROM_START + sizeof(float), energyEEPROMPointer);
    if (energyEEPROMPointer < ENERGY_EEPROM_START + sizeof(float) + sizeof(int) || 
        energyEEPROMPointer >= ENERGY_EEPROM_END) {
      energyEEPROMPointer = ENERGY_EEPROM_START + sizeof(float) + sizeof(int);
    }
    
  } else {
    // First time setup - save defaults
    energyOffset = 0;
    saveToEEPROM();
  }
}

void saveToEEPROM() {
  byte initialized = 123;
  EEPROM.put(0, initialized);
  int address = 1;
  
  // Save domestic bands
  for (int i = 0; i < NUM_DOMESTIC_BANDS; i++) {
    EEPROM.put(address, domesticBands[i]);
    address += sizeof(DomesticTariffBand);
  }
  
  // Save industrial bands
  for (int i = 0; i < NUM_INDUSTRIAL_BANDS; i++) {
    EEPROM.put(address, industrialBands[i]);
    address += sizeof(IndustrialTariffBand);
  }
  
  // *** FIXED: Save energy and pointer when saving settings ***
  EEPROM.put(ENERGY_EEPROM_START, energy);
  EEPROM.put(ENERGY_EEPROM_START + sizeof(float), energyEEPROMPointer);
  
  lcd.noCursor();
  lcd.clear();
  lcd.print("Settings Saved");
  delay(500);
}