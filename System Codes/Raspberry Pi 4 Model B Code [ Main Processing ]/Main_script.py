#!/usr/bin/env python3
"""
Automatic Note Detection and Sorting System
- Detects notes using camera and AI model
- Sends detection results to Firebase and Arduino
- Controls automatic sorting cycle
- Handles confidence levels and error cases
"""

import firebase_admin
from firebase_admin import credentials, db
import cv2
import numpy as np
import os
import serial
import time
import threading
import tflite_runtime.interpreter as tflite
from picamera2 import Picamera2
from datetime import datetime
import RPi.GPIO as GPIO

# Initialize model
interpreter = tflite.Interpreter(model_path="currency_model.tflite")
interpreter.allocate_tensors()
input_details = interpreter.get_input_details()
output_details = interpreter.get_output_details()

# Class labels (must match training order)
classes = ['100', '1000', '20', '50', '500', '5000', 'invalid_object', 'no_note']

# Firebase setup
cred = credentials.Certificate("/home/CASHM/firebaseKey.json")
firebase_admin.initialize_app(cred, {
    'databaseURL': 'https://hardware-project-be-default-rtdb.asia-southeast1.firebasedatabase.app/'
})

# Arduino serial communication (Note sorting system)
# Note: Note system uses /dev/ttyACM0, Coin system uses /dev/ttyACM1
try:
    arduino = serial.Serial('/dev/ttyACM0', 115200, timeout=10)
    time.sleep(2)  # Wait for Arduino to initialize
    print("✅ Note sorting Arduino connected on /dev/ttyACM0")
except:
    try:
        arduino = serial.Serial('/dev/ttyUSB0', 115200, timeout=10)
        time.sleep(2)
        print("✅ Note sorting Arduino connected on /dev/ttyUSB0 (fallback)")
    except:
        try:
            arduino = serial.Serial('/dev/ttyACM1', 115200, timeout=10)
            time.sleep(2)
            print("✅ Note sorting Arduino connected on /dev/ttyACM1 (fallback)")
        except:
            print("❌ Note sorting Arduino connection failed!")
            print("💡 Make sure note sorting Arduino is connected")
            print("💡 Coin Arduino should be on /dev/ttyACM1")
            arduino = None

# Camera setup
picam2 = Picamera2()
config = picam2.create_still_configuration(
    main={"size": (1920, 1080), "format": "RGB888"},
    controls={"AwbEnable": True, "AfMode": 1, "FrameRate": 30}
)
picam2.configure(config)

# Detection area parameters
DETECTION_WIDTH = 956
DETECTION_HEIGHT = 450
CENTER_X = 910
CENTER_Y = 325

# GPIO Button Configuration
ENTER_BUTTON_PIN = 18    # Start sorting button (Enter equivalent)
VIEW_BUTTON_PIN = 16     # View compartments button ('v' equivalent)
HOME_BUTTON_PIN = 20     # Home system button ('h' equivalent)
DEBOUNCE_TIME = 0.3      # 300ms debounce for reliable detection

# System state tracking
sorting_in_progress = False
last_button_press_time = {'enter': 0, 'view': 0, 'home': 0}

# Button state tracking
button_last_press_times = {
    ENTER_BUTTON_PIN: 0,
    VIEW_BUTTON_PIN: 0,
    HOME_BUTTON_PIN: 0
}

# Setup GPIO buttons
print("🔘 Setting up push buttons...")
try:
    GPIO.setmode(GPIO.BCM)
    GPIO.setwarnings(False)
    GPIO.setup(ENTER_BUTTON_PIN, GPIO.IN, pull_up_down=GPIO.PUD_UP)
    GPIO.setup(VIEW_BUTTON_PIN, GPIO.IN, pull_up_down=GPIO.PUD_UP)
    GPIO.setup(HOME_BUTTON_PIN, GPIO.IN, pull_up_down=GPIO.PUD_UP)
    print(f"✅ Enter button: GPIO{ENTER_BUTTON_PIN}")
    print(f"✅ View button: GPIO{VIEW_BUTTON_PIN}")
    print(f"✅ Home button: GPIO{HOME_BUTTON_PIN}")
except Exception as e:
    print(f"❌ Button setup failed: {e}")

def check_button_press(pin):
    """Check for debounced button press"""
    global button_last_press_times

    current_time = time.time()

    # Check if button is pressed (LOW due to pull-up)
    if GPIO.input(pin) == GPIO.LOW:
        # Check if enough time has passed since last press (debouncing)
        if (current_time - button_last_press_times[pin]) > DEBOUNCE_TIME:
            button_last_press_times[pin] = current_time
            return True

    return False

def check_all_buttons():
    """Check all buttons and return which one was pressed"""
    if check_button_press(ENTER_BUTTON_PIN):
        return "enter"
    elif check_button_press(VIEW_BUTTON_PIN):
        return "view"
    elif check_button_press(HOME_BUTTON_PIN):
        return "home"
    return None

def send_to_firebase(note_value, confidence, status):
    """Send detection result to Firebase"""
    try:
        note_ref = db.reference("/sorting_log")
        note_ref.push({
            'value': note_value,
            'confidence': float(confidence * 100),
            'status': status,
            'timestamp': datetime.now().isoformat()
        })
        print(f"📤 Sent to Firebase: {note_value} ({confidence*100:.1f}%)")
    except Exception as e:
        print(f"❌ Firebase error: {e}")

def send_to_arduino(note_value):
    """Send note value to Arduino and wait for DONE response"""
    if not arduino:
        print("❌ No Arduino connection")
        return False
    
    try:
        # Send note value
        arduino.write(f"{note_value}\n".encode())
        print(f"📤 Sent to Arduino: {note_value}")
        
        # Wait for DONE response
        print("⏳ Waiting for Arduino to complete sorting...")
        start_time = time.time()
        
        while time.time() - start_time < 120:  # 2 minute timeout
            if arduino.in_waiting > 0:
                response = arduino.readline().decode().strip()
                print(f"📥 Arduino: {response}")
                
                if "DONE" in response or "Ready. Enter note value:" in response:
                    print("✅ Arduino sorting complete!")
                    return True
                elif "ERROR" in response:
                    print("❌ Arduino reported error!")
                    return False
            
            time.sleep(0.1)
        
        print("⏰ Arduino timeout!")
        return False
        
    except Exception as e:
        print(f"❌ Arduino communication error: {e}")
        return False

def view_compartment():
    """Move to compartment viewing position (Y+ 21500)"""
    global sorting_in_progress

    if sorting_in_progress:
        print("⚠️ Cannot view compartment while sorting is in progress!")
        return False

    if not arduino:
        print("❌ No Arduino connection")
        return False

    try:
        print("🔄 Moving to compartment viewing position...")
        arduino.write("VIEW_COMPARTMENT\n".encode())
        print("📤 Sent VIEW_COMPARTMENT command to Arduino")

        # Wait for completion
        start_time = time.time()
        while time.time() - start_time < 60:  # 1 minute timeout
            if arduino.in_waiting > 0:
                response = arduino.readline().decode().strip()
                print(f"📥 Arduino: {response}")

                if "COMPARTMENT_VIEW_DONE" in response:
                    print("✅ Moved to compartment viewing position (Y+ 21500)")
                    print("🔍 You can now see the compartments from outside")
                    print("💡 Press 'h' + Enter to return home")
                    return True
                elif "ERROR" in response:
                    print("❌ Arduino error!")
                    return False
            time.sleep(0.1)

        print("⏰ Arduino timeout!")
        return False

    except Exception as e:
        print(f"❌ Compartment viewing error: {e}")
        return False

def home_system():
    """Return system to home position"""
    if not arduino:
        print("❌ No Arduino connection")
        return False

    try:
        print("🏠 Returning to home position...")
        arduino.write("HOME\n".encode())
        print("📤 Sent HOME command to Arduino")

        # Wait for completion
        start_time = time.time()
        while time.time() - start_time < 60:  # 1 minute timeout
            if arduino.in_waiting > 0:
                response = arduino.readline().decode().strip()
                print(f"📥 Arduino: {response}")

                if "HOME_DONE" in response:
                    print("✅ System returned to home position")
                    return True
                elif "ERROR" in response:
                    print("❌ Arduino error!")
                    return False
            time.sleep(0.1)

        print("⏰ Arduino timeout!")
        return False

    except Exception as e:
        print(f"❌ Homing error: {e}")
        return False

def capture_and_detect():
    """Capture image and detect note"""
    try:
        os.makedirs("sorting_images", exist_ok=True)
        
        # Capture image
        picam2.start()
        time.sleep(1)  # Camera stabilization
        raw_path = "sorting_images/raw_capture.jpg"
        picam2.capture_file(raw_path)
        picam2.stop()
        
        # Load and crop detection area
        full_img = cv2.imread(raw_path)
        cropped = full_img[
            CENTER_Y - DETECTION_HEIGHT // 2:CENTER_Y + DETECTION_HEIGHT // 2,
            CENTER_X - DETECTION_WIDTH // 2:CENTER_X + DETECTION_WIDTH // 2
        ]
        
        # Save cropped image
        timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
        cropped_path = f"sorting_images/detect_{timestamp}.jpg"
        cv2.imwrite(cropped_path, cropped)
        
        # Preprocess and predict
        input_data = preprocess_image(cropped)
        input_data = np.expand_dims(input_data, axis=0).astype(np.float32)
        
        interpreter.set_tensor(input_details[0]['index'], input_data)
        interpreter.invoke()
        predictions = interpreter.get_tensor(output_details[0]['index'])[0]
        
        # Get prediction
        class_id = np.argmax(predictions)
        confidence = np.max(predictions)
        predicted_class = classes[class_id]
        
        # Clean up
        os.remove(raw_path)
        
        print(f"🔍 Detected: {predicted_class} (Confidence: {confidence*100:.1f}%)")
        return predicted_class, confidence, cropped_path
        
    except Exception as e:
        print(f"❌ Detection error: {e}")
        return None, None, None

def preprocess_image(img):
    """Preprocess image for model"""
    try:
        img = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)
        img = cv2.resize(img, (224, 224))
        return (img / 255.0).astype(np.float32)
    except Exception as e:
        print(f"❌ Preprocessing error: {e}")
        return None

def detection_cycle():
    """Run continuous detection and sorting cycle"""
    global sorting_in_progress

    print("\n🔄 Starting detection cycle...")
    sorting_in_progress = True
    cycle_count = 0
    
    while True:
        cycle_count += 1
        print(f"\n--- Detection Cycle #{cycle_count} ---")
        
        # Detect note
        predicted_class, confidence, image_path = capture_and_detect()
        
        if predicted_class is None:
            print("❌ Detection failed, continuing...")
            continue
        
        # Handle different confidence levels and classes
        if predicted_class in ['no_note', 'invalid_object']:
            # Stop conditions
            if predicted_class == 'no_note':
                print("✅ No more notes detected - Sorting complete!")
                send_to_firebase('no_note', confidence, 'sorting_complete')

                # Send NO_NOTE command to Arduino to move to visible position
                print("📤 Sending NO_NOTE command to Arduino...")
                if send_to_arduino("NO_NOTE"):
                    print("✅ Arduino moved to visible position (Y+ 21500)")
                else:
                    print("❌ Failed to move to visible position")
            else:
                print("⚠️ Invalid object detected - Stopping for safety!")
                send_to_firebase('invalid_object', confidence, 'invalid_object_detected')
            break
            
        elif confidence < 0.60:
            # Low confidence - stop and alert
            print("❌ Low confidence detection - Invalid note placement!")
            print("Please check note placement and restart.")
            send_to_firebase(predicted_class, confidence, 'low_confidence_invalid')
            break
            
        elif 0.60 <= confidence < 0.7199:
            # Medium confidence - stop and ask for adjustment
            print("⚠️ Medium confidence - Please adjust note placement")
            print("Press Enter to continue or Ctrl+C to stop...")
            try:
                input()
                send_to_firebase(predicted_class, confidence, 'medium_confidence_adjusted')
            except KeyboardInterrupt:
                print("\n🛑 Stopped by user")
                break
            continue
            
        else:
            # High confidence - proceed with sorting
            print(f"✅ High confidence - Proceeding with sorting {predicted_class}")
            send_to_firebase(predicted_class, confidence, 'sorting_started')
            
            # Send to Arduino and wait for completion
            if send_to_arduino(predicted_class):
                send_to_firebase(predicted_class, confidence, 'sorting_completed')
                print(f"✅ {predicted_class} note sorted successfully!")
                print("🔄 Continuing to next note...")
            else:
                print("❌ Arduino sorting failed!")
                send_to_firebase(predicted_class, confidence, 'sorting_failed')
                print("🛑 Stopping due to Arduino error")
                break
    
    print("\n🏁 Detection cycle ended")
    sorting_in_progress = False

def main():
    """Main program loop"""
    print("🎯 Automatic Note Detection and Sorting System")
    print("=" * 50)
    print("📋 Confidence Levels:")
    print("  < 60%: Invalid note - Stop")
    print("  60-79.99%: Adjust placement - Pause")
    print("  ≥ 80%: Proceed with sorting")
    print("📋 Stop Conditions:")
    print("  - no_note: Sorting complete (stays at home)")
    print("  - invalid_object: Safety stop")
    print("📋 Controls:")
    print("  - Enter: Start sorting cycle")
    print("  - 'v' + Enter: View compartments (Y+ 21500)")
    print("  - 'h' + Enter: Return home")
    print("=" * 50)
    
    while True:
        try:
            print(f"\n🎛️ Status: {'🔄 SORTING' if sorting_in_progress else '⏸️ IDLE'}")
            print("🚀 Controls:")
            print("  - Press Enter OR GPIO18 button: Start sorting cycle")
            print("  - Type 'v' + Enter OR GPIO16 button: View compartments")
            print("  - Type 'h' + Enter OR GPIO20 button: Return home")
            print("  - Ctrl+C: Exit")

            # Check for button presses (non-blocking)
            button_pressed = check_all_buttons()

            if button_pressed == "enter":
                print("🔘 Enter button (GPIO18) pressed!")
                detection_cycle()
                continue
            elif button_pressed == "view":
                print("🔘 View button (GPIO16) pressed!")
                view_compartment()
                continue
            elif button_pressed == "home":
                print("🔘 Home button (GPIO20) pressed!")
                home_system()
                continue

            # Check for keyboard input (with timeout)
            import select
            import sys

            print("Enter command (or press button): ", end="", flush=True)

            # Wait for either keyboard input or button press
            ready, _, _ = select.select([sys.stdin], [], [], 0.1)  # 100ms timeout

            if ready:
                user_input = input().strip().lower()

                if user_input == "":
                    # Enter pressed - start detection cycle
                    detection_cycle()
                elif user_input == "v":
                    # View compartments
                    view_compartment()
                elif user_input == "h":
                    # Return home
                    home_system()
                else:
                    print("❌ Invalid command! Use Enter, 'v', or 'h'")
            else:
                # No keyboard input, continue checking buttons
                time.sleep(0.01)
            
        except KeyboardInterrupt:
            print("\n\n👋 Shutting down system...")
            if arduino:
                arduino.close()
            # Cleanup GPIO
            try:
                GPIO.cleanup()
                print("✅ GPIO cleaned up")
            except:
                pass
            break
        except Exception as e:
            print(f"❌ System error: {e}")
            continue

if __name__ == "__main__":
    main()

