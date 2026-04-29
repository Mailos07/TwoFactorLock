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

void display_capturing_password(int digits_entered) {
    u8g2.clearBuffer();

    u8g2.drawFrame(0, 0, 128, 64);

    u8g2.setFont(u8g2_font_7x13B_tr);
    const char* title = "ENTER CODE";
    u8g2.drawStr((128 - u8g2.getStrWidth(title)) / 2, 16, title);

    // Rounded box containing the 4 slots
    u8g2.drawRFrame(26, 24, 76, 22, 3);

    // 4 slots: filled disc = entered, empty circle = pending
    const int cy = 35;
    const int r  = 4;
    const int cx[4] = {40, 56, 72, 88};

    for (int i = 0; i < 4; i++) {
        if (i < digits_entered) {
            u8g2.drawDisc(cx[i], cy, r);
        } else {
            u8g2.drawCircle(cx[i], cy, r);
        }
    }

    u8g2.sendBuffer();
}

void display_code_incorrect() {
    u8g2.clearBuffer();

    // Screen border
    u8g2.drawFrame(0, 0, 128, 64);

    // Circle with X mark
    u8g2.drawCircle(64, 18, 12);
    u8g2.drawLine(57, 11, 71, 25);
    u8g2.drawLine(71, 11, 57, 25);

    u8g2.setFont(u8g2_font_7x13B_tr);
    u8g2.drawStr((128 - u8g2.getStrWidth("WRONG CODE")) / 2, 46, "WRONG CODE");
    u8g2.drawStr((128 - u8g2.getStrWidth("Try Again")) / 2, 60, "Try Again");

    u8g2.sendBuffer();
}

void display_code_correct() {
    u8g2.clearBuffer();

    // Screen border
    u8g2.drawFrame(0, 0, 128, 64);

    // Circle with checkmark
    u8g2.drawCircle(64, 18, 12);
    u8g2.drawLine(57, 18, 62, 23);
    u8g2.drawLine(62, 23, 71, 11);

    u8g2.setFont(u8g2_font_7x13B_tr);
    u8g2.drawStr((128 - u8g2.getStrWidth("CODE CORRECT")) / 2, 46, "CODE CORRECT");

    u8g2.sendBuffer();
}

void display_say_color_screen(int digit) {
    u8g2.clearBuffer();

    u8g2.drawFrame(0, 0, 128, 64);

    u8g2.setFont(u8g2_font_5x8_tr);
    const char* line1 = "Speak the color";
    const char* line2 = "corresponding to";
    const char* line3 = "this number:";
    u8g2.drawStr((128 - u8g2.getStrWidth(line1)) / 2, 10, line1);
    u8g2.drawStr((128 - u8g2.getStrWidth(line2)) / 2, 20, line2);
    u8g2.drawStr((128 - u8g2.getStrWidth(line3)) / 2, 30, line3);

    char digitStr[2] = {(char)('0' + digit), '\0'};
    u8g2.setFont(u8g2_font_logisoso28_tn);
    u8g2.drawStr((128 - u8g2.getStrWidth(digitStr)) / 2, 62, digitStr);

    u8g2.sendBuffer();
}