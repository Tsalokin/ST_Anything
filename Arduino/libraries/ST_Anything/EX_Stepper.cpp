//******************************************************************************************
//  File: EX_Stepper.cpp
//  Authors: Dan G Ogorchock
//
//  Summary:  EX_Servo is a class which implements the SmartThings/Hubitat "Switch Level" device capability.
//			  It inherits from the st::Executor class.
//
//			  Create an instance of this class in your sketch's global variable section
//			  For Example:  st::EX_Stepper executor1(F("stepper1"), PIN_SERVO, 90, true, 1000, 0, 180, 2000, 544, 2400);
//
//			  st::EX_Servo() constructor requires the following arguments
//				- String &name - REQUIRED - the name of the object - must match the Groovy ST_Anything DeviceType tile name
//				- byte pin_pwm - REQUIRED - the Arduino Pin to be used as a pwm output
//				- int startingAngle - OPTIONAL - the value desired for the initial angle of the servo motor (0 to 180, defaults to 90)
//              - bool detachAfterMove - OPTIONAL - determines if servo motor is powered down after move (defaults to false) 
//              - int servoDetachTime - OPTIONAL - determines how long after the servo is moved that the servo is powered down if detachAfterMove is true (defaults to 1000ms)
//				- int minLevelAngle - OPTIONAL - servo angle in degrees to map to level 0 (defaults to 0 degrees)
//				- int maxLevelAngle - OPTIONAL - servo angle in degrees to map to level 100 (defaults to 180 degrees)
//              - int servoRate - OPTIONAL - initial servo rate in ms/degree (defaults to 2000, used to ensure a gentle move during startup, afterwards comes from SmartThings/Hubitat with each move request)
//              - int minPulseWidth - OPTIONAL - minimum pulse width in milliseconds, defaults to 544 (see Arduino servo attach() function)
//              - int maxPulseWidth - OPTIONAL - maximum pulse width in milliseconds, defaults to 2400 (see Arduino servo attach() function)
//
//  Change History:
//
//    Date        Who            What
//    ----        ---            ----
//    2018-06-23  Dan Ogorchock  Original Creation
//    2018-06-24  Dan Ogorchock  Since ESP32 does not support SERVO library, exclude all code to prevent compiler error
//    2018-08-19  Dan Ogorchock  Added feature to optionally allow servo to be powered down after a move
//	  2019-02-02  Jeff Albers	 Added Parameters to map servo endpoints, actively control rate of servo motion via duration input to device driver, intializes to level instead of angle
//    2019-02-09  Dan Ogorchock  Adding Asynchronous Motion to eliminate blocking calls and to allow simultaneous motion across multiple servos
//    2019-03-04  Dan Ogorchock  Added optional min and max pulse width parameters to allow servo specific adjustments
//
//
//******************************************************************************************
#include "EX_Stepper.h"

#include "Constants.h"
#include "Everything.h"

#if not defined(ARDUINO_ARCH_ESP32)

namespace st
{
	//private
	void EX_Stepper::calcMotorPosition()
	{
		if(m_bMoveActive == true) {
			m_bMoveActive = false;
		}

		m_Stepper.enableOutputs();

		if ((m_nTargetAngle < m_nMinLevelAngle) and (m_nTargetAngle < m_nMaxLevelAngle)) {
			if (m_nMinLevelAngle < m_nMaxLevelAngle) {
				m_nTargetAngle = m_nMinLevelAngle;
			}
			else {
				m_nTargetAngle = m_nMaxLevelAngle;
			}
		}
		if 	((m_nTargetAngle > m_nMaxLevelAngle) and (m_nTargetAngle > m_nMinLevelAngle)) {
			if (m_nMaxLevelAngle > m_nMinLevelAngle) {
				m_nTargetAngle = m_nMaxLevelAngle;
			}
			else {
				m_nTargetAngle = m_nMinLevelAngle;
			}
		}

		m_nTimeStep =  abs(m_nCurrentRate / (m_nMaxLevelAngle - m_nMinLevelAngle));  //Constant servo step rate assumes duration is the time desired for maximum level change of 100
		m_nCurrentAngle = m_nOldAngle;             //preserver original angular position
		m_bMoveActive = true;                      //start the move (the update() function will take care of the actual motion)

		if (st::Executor::debug) {
			Serial.print(F("EX_Servo:: Servo motor angle set to "));
			Serial.println(m_nTargetAngle);
		}

	}

	//public
	//constructor
	EX_Stepper::EX_Stepper(const __FlashStringHelper *name, byte pinStep, byte pinDir, byte pinEnable, int startingAngle, bool disableAfterMove, long servoDisableTime, int minLevelAngle, int maxLevelAngle, int stepRate) :
		Executor(name),
		m_Stepper(AccelStepper::DRIVER, pinStep, pinDir),
		m_nTargetAngle(startingAngle),
		m_bDisableAfterMove(disableAfterMove),
		m_nDisableTime(servoDisableTime),
		m_nMinLevelAngle(minLevelAngle),
		m_nMaxLevelAngle(maxLevelAngle),
		m_nCurrentRate(stepRate),
		m_bMoveActive(false),
		m_bDisableTmrActive(false)
	{
		setEnablePin(pinEnable);
		m_nOldAngle = (minLevelAngle + maxLevelAngle) / 2;
	}

	//destructor
	EX_Stepper::~EX_Stepper()
	{

	}

	void EX_Stepper::init()
	{
		m_Stepper.setEnablePin(m_nPinEn);
		m_Stepper.setPinsInverted(false,false,true);
		m_Stepper.setAcceleration(100);
		m_Stepper.setMaxSpeed(m_nCurrentRate);
		calcMotorPosition();
		refresh();
	}

	void EX_Stepper::update()
	{
		if (m_bMoveActive) {
			
			m_Stepper.moveTo(m_nTargetAngle);
			
			if(m_Stepper.targetPosition() == m_Stepper.currentPosition()){
				m_bMoveActive = false;
				if (st::Executor::debug) {
					Serial.println(F("EX_Servo::update() move complete"));
				}
				if (m_bDisableAfterMove) { 
					m_bDisableTmrActive = true;
				}
				refresh();
			}
			
		}
		m_Stepper.run();

		if (m_bDisableTmrActive) {
			if ((millis() - m_nPrevMillis) > m_nDisableTime) {
				m_bDisableTmrActive = false;
				m_Stepper.disableOutputs();
				if (st::Executor::debug) {
					Serial.println(F("EX_Servo::update() detach complete"));
				}
			}
		}
	}

	void EX_Stepper::beSmart(const String &str)  
	{	
		String level = "";
		String rate = "";
		String max = "";
		String min = "";

		if(str.indexOf('!') == -1){
			level = str.substring(str.indexOf(' ') + 1, str.indexOf(':'));
			rate = str.substring(str.indexOf(':') + 1, str.indexOf('+'));
			max = str.substring(str.indexOf('+') + 1, str.indexOf('-'));
			min = str.substring(str.indexOf('-') + 1);
		}
		
		level.trim();
		rate.trim();
		min.trim();
		max.trim();
		
		if (st::Executor::debug) {
			Serial.print(F("EX_Servo::beSmart level = "));
			Serial.println(level);
			Serial.print(F("EX_Servo::beSmart rate = "));
			Serial.println(rate);
			Serial.print(F("EX_Servo::beSmart min = "));
			Serial.println(min);
			Serial.print(F("EX_Servo::beSmart max = "));
			Serial.println(max);
		}
				
		m_nCurrentLevel = int(level.toInt());
		m_nCurrentRate = long(rate.toInt());
		m_Stepper.setMaxSpeed(m_nCurrentRate);
		m_nMaxLevelAngle = long(max.toInt());
		m_nMinLevelAngle = long(min.toInt());
		m_nOldAngle = m_nCurrentAngle;
		m_nTargetAngle = map(m_nCurrentLevel, 0, 100, m_nMinLevelAngle, m_nMaxLevelAngle);

		if (st::Executor::debug) {
			Serial.print(F("EX_Servo::beSmart OldAngle = "));
			Serial.println(m_nOldAngle);
			Serial.print(F("EX_Servo::beSmart TargetAngle = "));
			Serial.println(m_nTargetAngle);
			Serial.print(F("EX_Servo::beSmart CurrentRate = "));
			Serial.println(m_nCurrentRate);
		}
		calcMotorPosition();
		

	}

	void EX_Stepper::refresh()
	{
		Everything::sendSmartString(getName() + " " + String(m_nCurrentLevel) + ":" + String(m_nTargetAngle) + ":" + String(m_nCurrentRate));
	}

	void EX_Stepper::setEnablePin(byte pin)
	{
		m_nPinEn = pin;
	}
}

#endif
