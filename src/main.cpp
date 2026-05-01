#include <Arduino.h>
#include <IRremote.hpp>
#include <map>
#include <string>
#include <array>

#include <SdFat.h>

/* ── Edge Impulse / I2S (voice keyword stage) ──────────────────────────── */
#include <ESP_I2S.h>
#include <Final-Project_inferencing.h>   // rename to match your EI export

#include "display.h"
#include "logging.h"

#define CODE_FILENAME "lock_config.txt"
#define SD_CARD_PIN D2
const byte IR_RECEIVE_PIN = A0;
SdFat g_sd;
bool g_sd_available = false;

/* ── PDM mic pins (onboard Sense — hardwired, do not change) ───────────── */
#define PDM_CLK_PIN   42   // GPIO 42 → MSM261D3526H1CPM CLK
#define PDM_DATA_PIN  41   // GPIO 41 → MSM261D3526H1CPM DATA

// MSM261D3526H1CPM power-up time ≤20 ms; 30 ms gives comfortable margin
#define WARMUP_MS     30

// DMA chunk size for each I2S read in the capture task
#define CAPTURE_CHUNK 2048

// Minimum EI confidence to accept a spoken keyword (0.0 – 1.0)
#define CONFIDENCE_THRESHOLD 0.7f

/* –– Track State ––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––– */
enum PROGRAM_STAGE {
  START,
  WAITING_FOR_CODE,
  CODE_ENTERED,
  CODE_CORRECT,
  CODE_INCORRECT,
  WAITING_FOR_KEYWORD,
  KEYWORD_CORRECT,
  KEYWORD_INCORRECT,
  UNLOCKED
};
PROGRAM_STAGE g_current_stage = START;

/* –– Globals ––––––––––––––––––––––––––––––––––––––––––––––––––––––––––––– */
std::map<int, std::string> g_keyword_map = {
  {1, "blue"},
  {2, "cyan"},
  {3, "green"},
  {4, "magenta"},
  {5, "red"},
  {6, "white"},
  {7, "yellow"}
};
const std::map<uint8_t, int> remoteDigits = 
    {{0x16, 0},
    {0x0C, 1},
    {0x18, 2},
    {0x5E, 3},
    {0x08, 4},
    {0x1C, 5},
    {0x5A, 6},
    {0x42, 7},
    {0x52, 8},
    {0x4A, 9}};
int keyword_int = 0;
int unlock_code = 0;

/* ── ML / audio globals ────────────────────────────────────────────────── */
typedef struct {
    int16_t  *buffer;
    uint8_t   buf_ready;
    uint32_t  buf_count;
    uint32_t  n_samples;
} inference_t;

static I2SClass    i2s;
static inference_t inference;
static int16_t     capture_buf[CAPTURE_CHUNK];
static bool        record_status = false;
static bool        debug_nn      = false;

/* –– Forward Declarations ––––––––––––––––––––––––––––––––––––––––––––––––––––– */
void start();
void waiting_for_code();
void read_code();
void check_code();
void code_incorrect();
void code_correct();
void waiting_for_keyword();
void keyword_correct();
void keyword_incorrect();
void unlocked();
void CapturePassword(int currDigit);

int HexToInt(uint8_t hexCode);
void SaveToSDCard(const String &data);
void DeleteCodeFile();
bool does_code_file_exist();
bool HasStoredPassword();
bool IsPasswordComplete();
void ResetPasswordCapture();
std::array<int, 4> get_file_password();
bool PasswordsMatch(const std::array<int, 4> &storedPassword);
std::pair<String, String> GenerateRandomColor();
String PasswordToString(const int digits[4]);
String PasswordToString(const std::array<int, 4> &digits);

/* ── ML / audio forward declarations ──────────────────────────────────── */
static bool i2s_init();
static bool inference_start(uint32_t n_samples);
static void inference_end();
static bool inference_record();
static void capture_task(void *arg);
static void audio_callback(uint32_t n_bytes);
static int  get_audio_signal_data(size_t offset, size_t length, float *out_ptr);

int passwordDigits[4] = {-1, -1, -1, -1}; // Array to hold the 4 digits of the password
int currentDigitIndex = 0;                // Index to track which digit is being entered
bool capturingPassword = false;
int expectedColorDigit = -1;
std::string expectedColorLabel = "";      // e.g. "magenta" — matched against EI label
bool isAppLocked = true;

using namespace std;


/* ══════════════════════════════════════════════════════════════════════════
 *  ML / I2S IMPLEMENTATION
 *  Adapted from the Edge Impulse continuous-audio-inference reference.
 *  Only the keyword-recognition stage uses this code; everything else is
 *  untouched from the colleague's latest push.
 * ══════════════════════════════════════════════════════════════════════════ */

/* ── PDM initialisation ────────────────────────────────────────────────── */
static bool i2s_init()
{
    i2s.setPinsPdmRx(PDM_CLK_PIN, PDM_DATA_PIN);

    bool ok = i2s.begin(I2S_MODE_PDM_RX,
                        EI_CLASSIFIER_FREQUENCY,
                        I2S_DATA_BIT_WIDTH_16BIT,
                        I2S_SLOT_MODE_MONO);
    if (!ok) {
        Serial.println("[ML] i2s.begin(PDM_RX) failed.");
        return false;
    }

    const size_t warmup_samples = (size_t)(EI_CLASSIFIER_FREQUENCY * WARMUP_MS / 1000);
    char *discard = (char *)malloc(warmup_samples * sizeof(int16_t));
    if (discard) {
        i2s.readBytes(discard, warmup_samples * sizeof(int16_t));
        free(discard);
    }
    return true;
}

/* ── Fill inference buffer one DMA chunk at a time ─────────────────────── */
static void audio_callback(uint32_t n_bytes)
{
    for (uint32_t i = 0; i < n_bytes >> 1; i++) {
        inference.buffer[inference.buf_count++] = capture_buf[i];
        if (inference.buf_count >= inference.n_samples) {
            inference.buf_count = 0;
            inference.buf_ready = 1;
        }
    }
}

/* ── FreeRTOS task: continuously reads I2S → inference buffer ───────────── */
static void capture_task(void *arg)
{
    const int32_t bytes_to_read = (uint32_t)arg;
    while (record_status) {
        size_t got = i2s.readBytes((char *)capture_buf, bytes_to_read);
        if (got <= 0) {
            Serial.printf("[ML] i2s.readBytes() returned %d\n", got);
        } else {
            if (got < (size_t)bytes_to_read)
                Serial.println("[ML] Partial I2S read");
            for (int x = 0; x < bytes_to_read / 2; x++)
                capture_buf[x] = (int16_t)(capture_buf[x]) * 8;
            if (record_status)
                audio_callback(bytes_to_read);
        }
    }
    vTaskDelete(NULL);
}

/* ── Allocate inference buffer and launch the capture task ─────────────── */
static bool inference_start(uint32_t n_samples)
{
    inference.buffer = (int16_t *)malloc(n_samples * sizeof(int16_t));
    if (!inference.buffer) return false;
    inference.buf_count = 0;
    inference.n_samples = n_samples;
    inference.buf_ready = 0;
    ei_sleep(100);
    record_status = true;
    xTaskCreate(capture_task, "CaptureTask", 1024 * 32, (void *)CAPTURE_CHUNK, 10, NULL);
    return true;
}

/* ── Block until a full 1-second window is captured ────────────────────── */
static bool inference_record()
{
    while (inference.buf_ready == 0) delay(10);
    inference.buf_ready = 0;
    return true;
}

/* ── Stop the capture task and free the inference buffer ───────────────── */
static void inference_end()
{
    record_status = false;
    ei_free(inference.buffer);
}

/* ── EI signal callback ─────────────────────────────────────────────────── */
static int get_audio_signal_data(size_t offset, size_t length, float *out_ptr)
{
    numpy::int16_to_float(inference.buffer + offset, out_ptr, length);
    return EIDSP_OK;
}


/* ══════════════════════════════════════════════════════════════════════════
 *  ORIGINAL PROGRAM — unchanged except where marked with [LOG]
 * ══════════════════════════════════════════════════════════════════════════ */

/* –– Setup & Loop –––––––––––––––––––––––––––––––––––––––––––––––––––––––––––– */
void setup() {
  Serial.begin(115200);
  while (!Serial.available()) {}

  // Init SD
  g_sd_available = g_sd.begin(SD_CARD_PIN);
  if (!g_sd_available) { Serial.println("SD initialization failed!"); }

  // Init logging
  logging_init(g_sd);

  // Init microphone — PDM mic for keyword inference
  if (!i2s_init()) {
    Serial.println("[ML][WARN] PDM init failed — voice stage will not work.");
  }

  // Init IR stuff
  Serial.println(F("Enabling IRin"));
  IrReceiver.begin(IR_RECEIVE_PIN, ENABLE_LED_FEEDBACK);
  Serial.print(F("Ready to receive IR signals at pin "));
  Serial.println(IR_RECEIVE_PIN);

  // Init display
  display_init();

  // Read code from lock_config.txt
  read_code();
}

void loop() {
  switch (g_current_stage) {
    case START:
      start();
      break;
    case WAITING_FOR_CODE:
      waiting_for_code();
      break;
    case CODE_ENTERED:
      check_code();
      break;
    case CODE_INCORRECT:
      code_incorrect();
      break;
    case CODE_CORRECT:
      code_correct();
      break;
    case WAITING_FOR_KEYWORD:
      waiting_for_keyword();
      break;
    case KEYWORD_CORRECT:
      keyword_correct();
      break;
    case KEYWORD_INCORRECT:
      keyword_incorrect();
      break;
    case UNLOCKED:
      unlocked();
      break;
    default:
      break;
  }
}

void read_code() {
  if (HasStoredPassword()) {
    Serial.println("Existing password found on SD card.");
  } else {
    Serial.println("No existing password found on SD card.");
  }
}

void start() {
  display_lock_screen();
  ResetPasswordCapture();
  capturingPassword = true;
  Serial.println(HasStoredPassword() ? "Enter stored password." : "Record a new password.");
  g_current_stage = WAITING_FOR_CODE;
}

void waiting_for_code() {
  if (!IrReceiver.decode()) {
    return;
  }

  int decimalValue = HexToInt(IrReceiver.decodedIRData.command);

  if (capturingPassword && decimalValue >= 0 && decimalValue <= 9) {
    Serial.println("Capturing password...");
    CapturePassword(decimalValue);

    if (IsPasswordComplete()) {
      g_current_stage = CODE_ENTERED;
    }
  }

  Serial.println("Decoded: " + String(decimalValue));
  delay(100);
  IrReceiver.resume();
}

/**
 * Check for stored (correct) password in code file
 * Compare with input password
 * Move to CODE_CORRECT or CODE_INCORRECT
 */
void check_code() {
  const bool hasStoredPassword = HasStoredPassword();
  const String capturedPassword = PasswordToString(passwordDigits);
  Serial.println("Captured: " + capturedPassword);

  if (!hasStoredPassword) {
    SaveToSDCard("CODE: " + capturedPassword);
    isAppLocked = false;
    ResetPasswordCapture();
    Serial.println("New password saved.");
    // [LOG] First-time password registration
    log_event_to_file("CODE", "PASS", "New password registered: " + capturedPassword);
    g_current_stage = UNLOCKED;
    return;
  }

  const auto storedPassword = get_file_password();
  if (PasswordsMatch(storedPassword)) {
    ResetPasswordCapture();
    // [LOG] Correct code entered — note digits are not logged for security
    log_event_to_file("CODE", "PASS", "Correct code entered");
    g_current_stage = CODE_CORRECT;
  } else {
    ResetPasswordCapture();
    // [LOG] Wrong code entered — log what was typed for audit trail
    log_event_to_file("CODE", "FAIL", "Incorrect code: " + capturedPassword);
    g_current_stage = CODE_INCORRECT;
  }
}

void code_incorrect() {
  isAppLocked = true;
  Serial.println("Password mismatch.");

  display_code_incorrect();
  delay(3000);
  display_lock_screen();
  ResetPasswordCapture();
  capturingPassword = true;
  Serial.println("Enter stored password.");
  g_current_stage = WAITING_FOR_CODE;
}

void code_correct() {
  display_code_correct();
  delay(3000);

  std::pair<String, String> generatedColor = GenerateRandomColor();
  expectedColorDigit = generatedColor.first.toInt();
  expectedColorLabel = g_keyword_map.at(expectedColorDigit);  // [ML]

  String message = generatedColor.second + ": (" + generatedColor.first + ")";
  Serial.println("Password accepted. Speak the color word: " + message);

  display_say_color_screen(expectedColorDigit);

  g_current_stage = WAITING_FOR_KEYWORD;
}

/* ── [ML] waiting_for_keyword ───────────────────────────────────────────────
 *
 * Records one second of audio, runs the Edge Impulse classifier on it, and
 * accepts the result if the top label matches expectedColorLabel with
 * confidence >= CONFIDENCE_THRESHOLD.
 * ─────────────────────────────────────────────────────────────────────────── */
void waiting_for_keyword() {
  Serial.println("[ML] Starting audio capture — speak the password word now.");

  if (!inference_start(EI_CLASSIFIER_RAW_SAMPLE_COUNT)) {
    Serial.println("[ML][ERROR] Could not allocate audio buffer.");
    // [LOG] System-level failure during voice stage
    log_event_to_file("VOICE", "FAIL", "Audio buffer allocation failed");
    g_current_stage = KEYWORD_INCORRECT;
    return;
  }

  if (!inference_record()) {
    Serial.println("[ML][ERROR] inference_record() failed.");
    inference_end();
    log_event_to_file("VOICE", "FAIL", "Audio capture failed");
    g_current_stage = KEYWORD_INCORRECT;
    return;
  }

  signal_t signal;
  signal.total_length = EI_CLASSIFIER_RAW_SAMPLE_COUNT;
  signal.get_data     = &get_audio_signal_data;

  ei_impulse_result_t result = {};
  EI_IMPULSE_ERROR err = run_classifier(&signal, &result, debug_nn);

  inference_end();

  if (err != EI_IMPULSE_OK) {
    Serial.printf("[ML][ERROR] run_classifier() returned %d\n", err);
    log_event_to_file("VOICE", "FAIL", "Classifier error: " + String(err));
    g_current_stage = KEYWORD_INCORRECT;
    return;
  }

  // Print all scores for debugging
  Serial.println("[ML] -- Scores ------------------------------------");
  for (uint16_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
    Serial.printf("  %-20s %.4f\n",
                  result.classification[ix].label,
                  result.classification[ix].value);
  }

  // Find the top-scoring label
  float   best_val = 0.0f;
  uint8_t best_idx = 0;
  for (uint16_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
    if (result.classification[ix].value > best_val) {
      best_val = result.classification[ix].value;
      best_idx = ix;
    }
  }

  const char *detected_label = result.classification[best_idx].label;
  Serial.printf("[ML] Best: %s @ %.1f%%\n", detected_label, best_val * 100.0f);

  // Build a detail string with detected word and confidence for the log
  // e.g. "heard:magenta,confidence:87.3%,expected:magenta"
  String detail = "heard:" + String(detected_label)
                + ",confidence:" + String(best_val * 100.0f, 1) + "%"
                + ",expected:" + String(expectedColorLabel.c_str());

  bool confidenceOk = (best_val >= CONFIDENCE_THRESHOLD);
  bool labelMatch   = (expectedColorLabel == std::string(detected_label));

  if (confidenceOk && labelMatch) {
    Serial.printf("[ML] KEYWORD ACCEPTED: %s\n", detected_label);
    // [LOG] Successful voice authentication with full detail
    log_event_to_file("VOICE", "PASS", detail);
    g_current_stage = KEYWORD_CORRECT;
  } else {
    if (!confidenceOk)
      Serial.printf("[ML] KEYWORD REJECTED: confidence too low (%.1f%% < %.0f%%)\n",
                    best_val * 100.0f, CONFIDENCE_THRESHOLD * 100.0f);
    else
      Serial.printf("[ML] KEYWORD REJECTED: heard '%s', expected '%s'\n",
                    detected_label, expectedColorLabel.c_str());
    // [LOG] Failed voice authentication with full detail
    log_event_to_file("VOICE", "FAIL", detail);
    g_current_stage = KEYWORD_INCORRECT;
  }
}

void keyword_incorrect() {
  isAppLocked = true;
  expectedColorDigit = -1;
  expectedColorLabel = "";
  Serial.println("Color mismatch. App remains locked.");
  display_lock_screen();
  ResetPasswordCapture();
  capturingPassword = true;
  Serial.println("Enter stored password.");
  g_current_stage = WAITING_FOR_CODE;
}

void keyword_correct() {
  isAppLocked = false;
  expectedColorDigit = -1;
  expectedColorLabel = "";
  Serial.println("Color accepted. App unlocked.");
  g_current_stage = UNLOCKED;
}

void unlocked() {
  display_unlock_screen();
  // App remains unlocked until reset
}

int HexToInt(uint8_t hexCode)
{
    auto it = remoteDigits.find(hexCode);
    return (it != remoteDigits.end()) ? it->second : hexCode;
}

void CapturePassword(int currDigit)
{
    if (currDigit < 0 || currDigit > 9)
    {
        return;
    }

    if (currentDigitIndex < 4)
    {
        passwordDigits[currentDigitIndex] = currDigit;
        currentDigitIndex++;
        
        //Convert Digits to "*" for display
        String displayPassword = "";
        for (int i = 0; i < currentDigitIndex; i++)
        {
            displayPassword += "*";
            display_capturing_password(currentDigitIndex);
        }
        Serial.println("Captured digit: " + String(currDigit) + " | Display: " + displayPassword);
    }
}

bool does_code_file_exist()
{
    if (!g_sd_available)
    {
        Serial.println("SD card not available; cannot check for file.");
        return false;
    }
    return g_sd.exists(CODE_FILENAME);
}

/**
 * Read code file, stripping CODE: prefix
 * 
 * @return std::array<int, 4> containing 4 password digits
 */
std::array<int, 4> get_file_password()
{
    std::array<int, 4> password = {-1, -1, -1, -1};
    if (!g_sd_available)
    {
        Serial.println("SD card not available; cannot read file.");
        return password;
    }

    FsFile file = g_sd.open(CODE_FILENAME, FILE_READ);
    if (file)
    {
        String line = file.readStringUntil('\n');
        file.close();
        int codeIndex = line.indexOf("CODE: ");
        if (codeIndex != -1)
        {
            String codeStr = line.substring(codeIndex + 6); // Get the part after "CODE: "
            if (codeStr.length() >= 4)
            {
                for (int i = 0; i < 4; i++)
                {
                    password[i] = codeStr.charAt(i) - '0'; // Convert char to int
                }
            }
        }
    }
    else
    {
        Serial.println("Error opening file on SD card.");
    }
    return password;
}

/**
 * Check if code file exists
 * Read code file for password
 */
bool HasStoredPassword()
{
    if (!does_code_file_exist())
    {
        return false;
    }

    const auto password = get_file_password();
    for (int digit : password)
    {
        if (digit < 0 || digit > 9)
        {
            return false;
        }
    }

    return true;
}

void SaveToSDCard(const String &data)
{
    if (!g_sd_available)
    {
        Serial.println("SD card not available; skipping password write.");
        return;
    }

    DeleteCodeFile();

    FsFile file = g_sd.open(CODE_FILENAME, FILE_WRITE);
    if (file)
    {
        file.println(data);
        file.close();
        Serial.println("Data saved to SD card.");
    }
    else
    {
        Serial.println("Error opening file on SD card.");
    }
}

void DeleteCodeFile()
{
    if (!g_sd_available)
    {
        return;
    }

    if (g_sd.exists(CODE_FILENAME) && g_sd.remove(CODE_FILENAME))
    {
        Serial.println("Code file cleared.");
    }
    else
    {
        Serial.println("No code file to clear or failed to remove it.");
    }
}

bool IsPasswordComplete()
{
    for (int digit : passwordDigits)
    {
        if (digit == -1)
        {
            return false;
        }
    }

    return true;
}

bool PasswordsMatch(const std::array<int, 4> &storedPassword)
{
    for (int i = 0; i < 4; i++)
    {
        if (passwordDigits[i] != storedPassword[i])
        {
            return false;
        }
    }

    return true;
}

void ResetPasswordCapture()
{
    capturingPassword = false;
    currentDigitIndex = 0;
    for (int i = 0; i < 4; i++)
    {
        passwordDigits[i] = -1;
    }
}

/**
 * Return pair of <String,String> representing <Index, Color>
 */
std::pair<String, String> GenerateRandomColor()
{
    int randomColorIndex = random(1, 8);
    return {String(randomColorIndex), String(g_keyword_map.at(randomColorIndex).c_str())};
}

String PasswordToString(const int digits[4])
{
    return String(digits[0]) + String(digits[1]) + String(digits[2]) + String(digits[3]);
}

String PasswordToString(const std::array<int, 4> &digits)
{
    return String(digits[0]) + String(digits[1]) + String(digits[2]) + String(digits[3]);
}

#if !defined(EI_CLASSIFIER_SENSOR) || EI_CLASSIFIER_SENSOR != EI_CLASSIFIER_SENSOR_MICROPHONE
#error "Invalid model for current sensor — export an audio/microphone model from Edge Impulse."
#endif