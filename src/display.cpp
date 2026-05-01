/**
 * @file display.cpp
 * @brief Methods to display different states on screen
 * @date 2026-04-22
 * 
 */
#include <Arduino.h>

// Display
#include <U8g2lib.h>
#include <Wire.h>

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);


void display_init() {

    Wire.begin();
    u8g2.begin();
    u8g2.setFlipMode(1);
    
}

void display_lock_screen() {
    u8g2.clearBuffer();

    // Screen border
    u8g2.drawFrame(0, 0, 128, 64);

    // Shackle: upper-half arc + two legs descending into body
    u8g2.drawCircle(64, 13, 10, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);
    u8g2.drawLine(54, 13, 54, 24);
    u8g2.drawLine(74, 13, 74, 24);

    // Lock body
    u8g2.drawRBox(48, 24, 32, 18, 2);

    // Keyhole cut-out
    u8g2.setDrawColor(0);
    u8g2.drawDisc(64, 30, 3);
    u8g2.drawBox(62, 33, 5, 5);
    u8g2.setDrawColor(1);

    // "LOCKED" centered at bottom
    u8g2.setFont(u8g2_font_7x13B_tr);
    u8g2.drawStr((128 - u8g2.getStrWidth("LOCKED")) / 2, 60, "LOCKED");

    u8g2.sendBuffer();
}

void display_unlock_screen() {
     u8g2.clearBuffer();

    // Screen border
    u8g2.drawFrame(0, 0, 128, 64);

    // Shackle: open — right leg stays in body, left leg lifts out
    // Right leg: connected to body
    u8g2.drawLine(74, 13, 74, 24);
    // Arc across the top (upper half only)
    u8g2.drawCircle(64, 13, 10, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);
    // Left leg: disconnected and angled upward-left (open position)
    //u8g2.drawLine(54, 13, 44, 4);

    // Lock body
    u8g2.drawRBox(48, 24, 32, 18, 2);

    // Keyhole cut-out
    u8g2.setDrawColor(0);
    u8g2.drawDisc(64, 30, 3);
    u8g2.drawBox(62, 33, 5, 5);
    u8g2.setDrawColor(1);

    // "UNLOCKED" centered at bottom
    u8g2.setFont(u8g2_font_7x13B_tr);
    u8g2.drawStr((128 - u8g2.getStrWidth("UNLOCKED")) / 2, 60, "UNLOCKED");

    u8g2.sendBuffer();
}

void display_message(const String &message) {
    u8g2.clearBuffer();

    // Screen border
    u8g2.drawFrame(0, 0, 128, 64);
    String sayMessage = "Say:";
    // Display message centered
    u8g2.setFont(u8g2_font_7x13B_tr);
    // Center "Say:" on the first line and the message on the second line
    u8g2.drawStr((128 - u8g2.getStrWidth(sayMessage.c_str())) / 2, 16, sayMessage.c_str());
    u8g2.drawStr((128 - u8g2.getStrWidth(message.c_str())) / 2, 32, message.c_str());

    u8g2.sendBuffer();
}

/* ── New display functions ─────────────────────────────────────────────── */

/**
 * @brief Show a large ✓ and "CODE OK" — displayed after correct code entry.
 */
void display_code_correct() {
    u8g2.clearBuffer();

    // Screen border
    u8g2.drawFrame(0, 0, 128, 64);

    // Large check mark built from two lines
    u8g2.drawLine(44, 32, 54, 44);
    u8g2.drawLine(54, 44, 80, 18);

    // Label at bottom
    u8g2.setFont(u8g2_font_7x13B_tr);
    u8g2.drawStr((128 - u8g2.getStrWidth("CODE OK")) / 2, 60, "CODE OK");

    u8g2.sendBuffer();
}

/**
 * @brief Show a large ✗ and "DENIED" — displayed after wrong code entry.
 */
void display_code_incorrect() {
    u8g2.clearBuffer();

    // Screen border
    u8g2.drawFrame(0, 0, 128, 64);

    // Large X
    u8g2.drawLine(48, 16, 80, 44);
    u8g2.drawLine(80, 16, 48, 44);

    // Label at bottom
    u8g2.setFont(u8g2_font_7x13B_tr);
    u8g2.drawStr((128 - u8g2.getStrWidth("DENIED")) / 2, 60, "DENIED");

    u8g2.sendBuffer();
}

/**
 * @brief Show the color number the user must speak.
 *        Displays the number prominently so the user can look up the word
 *        from the vocabulary list without the system revealing it.
 *
 * @param colorIndex  Integer 1–7 matching the Password Vocabulary list.
 */
void display_say_color_screen(int colorIndex) {
    u8g2.clearBuffer();

    // Screen border
    u8g2.drawFrame(0, 0, 128, 64);

    // Header
    u8g2.setFont(u8g2_font_7x13B_tr);
    u8g2.drawStr((128 - u8g2.getStrWidth("SAY COLOR #")) / 2, 16, "SAY COLOR #");

    // Color number — large font, centered
    u8g2.setFont(u8g2_font_logisoso28_tr);
    String numStr = String(colorIndex);
    u8g2.drawStr((128 - u8g2.getStrWidth(numStr.c_str())) / 2, 52, numStr.c_str());

    u8g2.sendBuffer();
}

/**
 * @brief Show asterisks representing digits entered so far during code entry.
 *        Prints one '*' per digit captured; remaining slots shown as '-'.
 *
 * @param digitCount  Number of digits entered so far (1–4).
 */
void display_capturing_password(int digitCount) {
    u8g2.clearBuffer();

    // Screen border
    u8g2.drawFrame(0, 0, 128, 64);

    // Header
    u8g2.setFont(u8g2_font_7x13B_tr);
    u8g2.drawStr((128 - u8g2.getStrWidth("ENTER CODE")) / 2, 16, "ENTER CODE");

    // Build display string: filled slots = '*', empty slots = '-'
    char progress[5] = {'-', '-', '-', '-', '\0'};
    for (int i = 0; i < digitCount && i < 4; i++) {
        progress[i] = '*';
    }

    // Wider font so the 4-char string is easy to read
    u8g2.setFont(u8g2_font_logisoso28_tr);
    u8g2.drawStr((128 - u8g2.getStrWidth(progress)) / 2, 52, progress);

    u8g2.sendBuffer();
}