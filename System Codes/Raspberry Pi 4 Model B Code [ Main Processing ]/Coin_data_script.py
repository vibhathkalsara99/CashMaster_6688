import serial
import firebase_admin
from firebase_admin import credentials, db

# Initialize Firebase
cred = credentials.Certificate("firebaseKey.json")
firebase_admin.initialize_app(cred, {
    'databaseURL': 'https://hardware-project-be-default-rtdb.asia-southeast1.firebasedatabase.app/'
})

# Connect to Arduino (Coin/Display system)
ser = serial.Serial('/dev/ttyACM1', 9600, timeout=1)

print("📡 Listening to Coin Arduino on /dev/ttyACM1...")
print("🔧 New Format Support:")
print("   - Arduino should send 'COIN:1', 'COIN:2', 'COIN:5', 'COIN:10' when coins are detected")
print("   - Legacy support for 'Rs.1', 'Rs.2', etc. still available")
print("   - Firebase structure remains: coin_totals/Rs_1, coin_totals/Rs_2, etc.")

# Function to sanitize coin name (Firebase-safe path)
def sanitize(coin_name):
    return coin_name.replace('.', '_')

# Function to update Firebase for coins
def update_coin_total(coin_value):
    coin_type = f"Rs.{coin_value}"  # Convert COIN:1 → Rs.1 for Firebase
    safe_type = sanitize(coin_type)  # e.g., Rs.1 → Rs_1
    ref = db.reference('coin_totals/' + safe_type)
    current = ref.get()
    if current is None:
        current = 0
    ref.set(current + 1)
    print(f"✅ Updated COIN:{coin_value} → Firebase: {safe_type} = {current + 1}")

# Function to update Firebase for notes
def update_note_total(note_value):
    note_type = f"Rs.{note_value}"  # Convert NOTE:100 → Rs.100 for Firebase
    safe_type = sanitize(note_type)  # e.g., Rs.100 → Rs_100
    ref = db.reference('note_totals/' + safe_type)
    current = ref.get()
    if current is None:
        current = 0
    ref.set(current + 1)
    print(f"✅ Updated NOTE:{note_value} → Firebase: {safe_type} = {current + 1}")

# Function to process detection data
def process_detection(detection_string):
    """Process detection and determine if it's coin or note based on value"""
    try:
        detection_string = detection_string.strip()
        
        # Handle new COIN: format
        if detection_string.startswith("COIN:"):
            value_str = detection_string.replace("COIN:", "")
            
            if not value_str.isdigit():
                print(f"⚠️ Invalid coin format: {detection_string}")
                return
                
            value = int(value_str)
            
            # Valid coin values
            if value in [1, 2, 5, 10]:
                print(f"🪙 Coin Detected: COIN:{value}")
                update_coin_total(value)
            else:
                print(f"⚠️ Unknown coin value: {detection_string}")
            return
            
        # Handle NOTE: format (if needed)
        elif detection_string.startswith("NOTE:"):
            value_str = detection_string.replace("NOTE:", "")
            
            if not value_str.isdigit():
                print(f"⚠️ Invalid note format: {detection_string}")
                return
                
            value = int(value_str)
            
            # Valid note values
            if value in [20, 50, 100, 500, 1000, 5000]:
                print(f"💵 Note Detected: NOTE:{value}")
                update_note_total(value)
            else:
                print(f"⚠️ Unknown note value: {detection_string}")
            return
            
        # Legacy Rs. format support
        elif detection_string.startswith("Rs."):
            value_str = detection_string.replace("Rs.", "")
            
            if not value_str.isdigit():
                print(f"⚠️ Invalid Rs. format: {detection_string}")
                return
                
            value = int(value_str)
            
            if value in [1, 2, 5, 10]:
                print(f"🪙 Legacy Coin: Rs.{value}")
                update_coin_total(value)
            elif value in [20, 50, 100, 500, 1000, 5000]:
                print(f"💵 Legacy Note: Rs.{value}")
                update_note_total(value)
            else:
                print(f"⚠️ Unknown Rs. value: {detection_string}")
            return
        
        # Not a recognized format
        print(f"ℹ️ Ignoring: {detection_string}")
        
    except Exception as e:
        print(f"❌ Error processing: {detection_string} - {e}")

def is_valid_detection(line):
    """Check if line contains valid detection data"""
    if not line:
        return False
    
    # Check COIN: format
    if line.startswith("COIN:"):
        value_part = line.replace("COIN:", "").strip()
        if value_part.isdigit():
            return int(value_part) in [1, 2, 5, 10]
    
    # Check NOTE: format
    if line.startswith("NOTE:"):
        value_part = line.replace("NOTE:", "").strip()
        if value_part.isdigit():
            return int(value_part) in [20, 50, 100, 500, 1000, 5000]
    
    # Check Rs. format
    if line.startswith("Rs."):
        value_part = line.replace("Rs.", "").strip()
        if value_part.isdigit():
            return int(value_part) in [1, 2, 5, 10, 20, 50, 100, 500, 1000, 5000]
    
    return False

# Main monitoring loop
print("\n🚀 Starting coin detection monitoring...")
print("💡 Supported formats:")
print("   • COIN:1, COIN:2, COIN:5, COIN:10 (new)")
print("   • Rs.1, Rs.2, Rs.5, Rs.10, Rs.20, Rs.50, Rs.100, Rs.500, Rs.1000, Rs.5000 (legacy)")
print("🛑 Press Ctrl+C to stop\n")

while True:
    try:
        # Read line from Arduino
        line = ser.readline().decode('utf-8', errors='ignore').strip()
        
        if line:
            if is_valid_detection(line):
                print(f"🔍 Valid Detection: {line}")
                process_detection(line)
            else:
                # Show Arduino messages but filter noise
                if len(line) > 3 and not any(prefix in line for prefix in ["COIN:", "NOTE:", "Rs."]):
                    print(f"📄 Arduino: {line}")
                elif len(line) == 1:
                    # Debug single characters
                    print(f"🔧 Debug: '{line}' (ASCII: {ord(line)})")
                elif 1 < len(line) <= 3:
                    print(f"🔧 Debug: Short data '{line}'")
                    
    except UnicodeDecodeError as e:
        print(f"⚠️ Encoding error: {e}")
        try:
            # Try to recover
            raw_data = ser.readline()
            line = raw_data.decode('utf-8', errors='replace').strip()
            if line and line != '\ufffd' and is_valid_detection(line):
                print(f"🔍 Recovered: {line}")
                process_detection(line)
        except Exception as recovery_error:
            print(f"❌ Recovery failed: {recovery_error}")
            
    except KeyboardInterrupt:
        print("\n🛑 Stopping coin detection monitoring...")
        break
        
    except Exception as e:
        print(f"❌ Communication error: {e}")
        try:
            ser.close()
            ser = serial.Serial('/dev/ttyACM1', 9600, timeout=1)
            print("🔄 Reconnected to Arduino")
        except Exception as reconnect_error:
            print(f"❌ Reconnection failed: {reconnect_error}")
            break

print("👋 Monitoring stopped")
