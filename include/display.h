#pragma once

void display_init();
void display_lock_screen();
void display_unlock_screen();
void display_message(const String &message);
void display_capturing_password(int digits_entered);
void display_code_incorrect();
void display_code_correct();
void display_say_color_screen(int digit);