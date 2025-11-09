```python
import machine
import time

# Global variables for interrupt-based trigger
check_for_trigger = False
web_server_running = False

# Different ways to trigger GPIO 34:
# 1. Use a jumper wire to connect GPIO 34 to 3.3V (HIGH) or GND (LOW).
# 2. Briefly touch GPIO 34 and 3.3V (or GND) together using tweezers for a momentary trigger.
# 3. Use a push button: one end to GPIO 34, the other to 3.3V (for HIGH) or GND (for LOW).
# 4. Use a DIP switch or toggle switch between GPIO 34 and 3.3V/GND.
# 5. Use an external circuit (e.g., relay, sensor output) to drive GPIO 34 HIGH or LOW.
# Note: GPIO 34 is input-only on ESP32 and should not be pulled above 3.3V.

# Initialize GPIO 34 as input with pull-down (main trigger)
gpio34 = machine.Pin(34, machine.Pin.IN, machine.Pin.PULL_DOWN)

# Initialize BOOT button (GPIO 0) as input with pull-up (alternative trigger)
boot_btn = machine.Pin(0, machine.Pin.IN, machine.Pin.PULL_UP)

def button_interrupt(pin):
    """Interrupt handler for BOOT button press"""
    global check_for_trigger
    check_for_trigger = True

def start_web_server():
    # ...existing code to start web server...
    print("Web server started!")
    global web_server_running
    web_server_running = True
    pass

# Legacy function - kept for reference (not used in interrupt mode)
def check_boot_button():
    """Check if BOOT button is pressed with debouncing (LEGACY)"""
    if boot_btn.value() == 0:  # BOOT button pressed (LOW due to pull-up)
        time.sleep_ms(50)  # Debounce delay
        if boot_btn.value() == 0:  # Confirm button is still pressed
            return True
    return False

def check_long_press(required_duration_ms=5000):
    """Check for 5-second long press trigger (interrupt-based)"""
    if boot_btn.value() == 0:  # Button pressed (LOW due to pull-up)
        print("🔘 Button pressed - hold for 5 seconds to start web server...")

        start_time = time.ticks_ms()
        last_feedback = 0

        while boot_btn.value() == 0:
            current_time = time.ticks_ms()
            elapsed = time.ticks_diff(current_time, start_time)

            # Check if duration reached
            if elapsed >= required_duration_ms:
                print("✅ 5-second long press SUCCESS! Starting web server...")

                # Wait for button release to prevent multiple triggers
                while boot_btn.value() == 0:
                    time.sleep_ms(10)

                return True

            # Enhanced visual feedback every second
            if elapsed // 1000 > last_feedback:
                last_feedback = elapsed // 1000
                remaining = (required_duration_ms - elapsed) // 1000

                if remaining > 0:
                    print(f"⏱️  Hold for {remaining} more seconds...")
                else:
                    print("⏱️  Almost there - keep holding...")

            time.sleep_ms(50)  # Check every 50ms

        # Button was released before duration
        elapsed = time.ticks_diff(time.ticks_ms(), start_time)
        print(f"❌ Button released after {elapsed/1000:.1f}s - trigger cancelled")

    return False

# Legacy function - replaced by interrupt-based approach
def check_trigger_methods():
    """Check all available trigger methods (LEGACY - not used in interrupt mode)"""

    # Method 1: GPIO 34 trigger (still used for startup)
    if gpio34.value() == 1:
        print("GPIO 34 HIGH detected - starting web server...")
        return True

    # Method 2: Quick BOOT button press (LEGACY)
    if check_boot_button():
        print("BOOT button quick press - starting web server...")
        return True

    # Method 3: Long press backup method (now interrupt-based)
    if check_long_press():
        print("Long press trigger detected - starting web server...")
        return True

    return False

# Setup interrupt on BOOT button for on-demand trigger
boot_btn.irq(trigger=machine.Pin.IRQ_FALLING, handler=button_interrupt)

# Main startup and continuous operation
print("🚀 ESP32 IoT Gateway Started")
print("📱 On-Demand Trigger: Press and hold BOOT button for 5 seconds to start web server")
print("🔧 Alternative: Connect GPIO 34 to 3.3V for instant trigger")

# Check GPIO 34 for instant startup trigger (if needed)
if gpio34.value() == 1:
    print("⚡ GPIO 34 HIGH detected at startup - starting web server immediately...")
    start_web_server()

# Main operation loop - interrupt-based trigger monitoring
print("🔄 System running - press BOOT button for 5 seconds anytime to enter config mode...")

while True:
    # Check if interrupt triggered and web server not already running
    if check_for_trigger and not web_server_running:
        print("🔘 Button press detected - checking for long press...")

        if check_long_press():
            start_web_server()

        # Reset trigger flag
        check_for_trigger = False

    # Main application tasks go here
    # (Replace this sleep with your main program logic)
    time.sleep_ms(100)  # Check every 100ms
