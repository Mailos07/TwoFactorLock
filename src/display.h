/**
 * @file display.h
 * @brief Display function declarations for the TwoFactorLock OLED interface
 * @date 2026-04-22
 */
#pragma once

#include <Arduino.h>

/* ── Initialisation ────────────────────────────────────────────────────── */
void display_init();

/* ── Lock / unlock screens ─────────────────────────────────────────────── */
void display_lock_screen();
void display_unlock_screen();

/* ── Generic message ───────────────────────────────────────────────────── */
void display_message(const String &message);

/* ── Code-entry feedback ───────────────────────────────────────────────── */
void display_capturing_password(int digitCount);
void display_code_correct();
void display_code_incorrect();

/* ── Voice / keyword stage ─────────────────────────────────────────────── */
void display_say_color_screen(int colorIndex);
