# CashMaster 6688

**CashMaster 6688** is an intelligent currency recognition and sorting system that automates the identification and organization of coins and notes. The system integrates Raspberry Pi, Arduino, sensors, and a web application to deliver precise and reliable currency management.

---

## Project Overview

CashMaster 6688 combines hardware and software components to streamline currency handling. The system uses **image recognition** for note detection and **IR sensors** for coin identification. It sorts and stores currency into designated compartments with actuator-controlled mechanisms.

A responsive web application monitors real-time currency data, provides user interaction, and maintains comprehensive logs.

---

## Features

* Automatic detection and sorting of coins and notes
* Real-time monitoring via a responsive web application
* Secure currency storage using solenoid locks
* Suction-based mechanism for note handling
* Comprehensive logging of transactions and denomination counts

---

## Technologies Used

* **Hardware:** Raspberry Pi 4 Model B, Arduino Uno, CNC Shield V3, Stepper motors, IR sensors, Solenoid locks
* **Software:**

  * Python + MobileNetV2 (Image Recognition & Core Logic)
  * Arduino C/C++ (Hardware control, sensors, actuators)
  * HTML, CSS, JavaScript (Web interface)
* **Database:** Firebase (for real-time currency data)
* **Communication:** USB Serial between Raspberry Pi and Arduinos
* **Libraries/Tools:** OpenCV (Image Processing), SPI LCD, Keypad I2C, A4988 Stepper Drivers

---

## System Architecture

1. **Currency Detection:**

   * Coins detected via IR sensors
   * Notes detected via Raspberry Pi Camera Module using image recognition

2. **Sorting Mechanism:**

   * CoreXY mechanism for precise note placement
   * Suction tube for picking and moving notes
   * Solenoid-controlled compartments for secure storage

3. **Web Application:**

   * Real-time display of total amount and denomination counts
   * Interface to unlock coin/note compartments as needed

---

## How the System Works

1. **Powering the Device**

   * Simply **plug the system normally**.
   * The built-in 12V power supply converts home AC current to 12V DC required by the system.

2. **Connecting to Wi-Fi**

   * The **Raspberry Pi** connects automatically to the pre-configured Wi-Fi network.
   * This enables communication with the **web application** and Firebase database for real-time monitoring.

3. **Running the Software**

   * All required Python and Arduino scripts are **pre-uploaded** to the Raspberry Pi and Arduino boards.
   * Once powered, the scripts run **automatically**, initializing sensors, camera, actuators, and the sorting logic.
   * The system is now ready to **detect and sort coins and notes**.

4. **Web Application Monitoring**

   * Open the web application: [https://hardware-project-be.web.app/index.html](https://hardware-project-be.web.app/index.html)
   * **Admin Login Credentials:**

     * Username: `admin`
     * Password: `cashmaster6688`
   * Use the admin panel to **monitor real-time currency data**, total amounts, and denomination counts.
   * The web interface also allows **control actions** such as unlocking coin/note compartments as needed.

---


