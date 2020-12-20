//============================================================================
//
//   SSSS    tt          lll  lll
//  SS  SS   tt           ll   ll
//  SS     tttttt  eeee   ll   ll   aaaa
//   SSSS    tt   ee  ee  ll   ll      aa
//      SS   tt   eeeeee  ll   ll   aaaaa  --  "An Atari 2600 VCS Emulator"
//  SS  SS   tt   ee      ll   ll  aa  aa
//   SSSS     ttt  eeeee llll llll  aaaaa
//
// Copyright (c) 1995-2020 by Bradford W. Mott, Stephen Anthony
// and the Stella Team
//
// See the file "License.txt" for information on usage and redistribution of
// this file, and for a DISCLAIMER OF ALL WARRANTIES.
//============================================================================

#include "Event.hxx"
#include "System.hxx"

#include "Driving.hxx"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Driving::Driving(Jack jack, const Event& event, const System& system, bool altmap)
  : Controller(jack, event, system, Controller::Type::Driving)
{
  if(myJack == Jack::Left)
  {
    if(!altmap)
    {
      myCCWEvent  = Event::JoystickZeroLeft;
      myCWEvent   = Event::JoystickZeroRight;
      myFireEvent = Event::JoystickZeroFire;
    }
    else
    {
      myCCWEvent  = Event::JoystickTwoLeft;
      myCWEvent   = Event::JoystickTwoRight;
      myFireEvent = Event::JoystickTwoFire;
    }
    myXAxisValue = Event::PaddleZeroAnalog;
    myYAxisValue = Event::PaddleOneAnalog;
  }
  else
  {
    if(!altmap)
    {
      myCCWEvent  = Event::JoystickOneLeft;
      myCWEvent   = Event::JoystickOneRight;
      myFireEvent = Event::JoystickOneFire;
    }
    else
    {
      myCCWEvent  = Event::JoystickThreeLeft;
      myCWEvent   = Event::JoystickThreeRight;
      myFireEvent = Event::JoystickThreeFire;
    }
    myXAxisValue = Event::PaddleTwoAnalog;
    myYAxisValue = Event::PaddleThreeAnalog;
  }

  // Digital pins 3 and 4 are not connected
  setPin(DigitalPin::Three, true);
  setPin(DigitalPin::Four, true);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Driving::update()
{
  // Digital events (from keyboard or joystick hats & buttons)
  bool firePressed = myEvent.get(myFireEvent) != 0;

  int d_axis = myEvent.get(myXAxisValue);
  if(myEvent.get(myCCWEvent) != 0 || d_axis < -16384)     --myCounter;
  else if(myEvent.get(myCWEvent) != 0 || d_axis > 16384)  ++myCounter;

  // Mouse motion and button events
  if(myControlID > -1)
  {
    int m_axis = myEvent.get(Event::MouseAxisXMove);
    if(m_axis < -2)     --myCounter;
    else if(m_axis > 2) ++myCounter;
    firePressed = firePressed
      || myEvent.get(Event::MouseButtonLeftValue)
      || myEvent.get(Event::MouseButtonRightValue);
  }
  else
  {
    // Test for 'untied' mouse axis mode, where each axis is potentially
    // mapped to a separate driving controller
    if(myControlIDX > -1)
    {
      int m_axis = myEvent.get(Event::MouseAxisXMove);
      if(m_axis < -2)     --myCounter;
      else if(m_axis > 2) ++myCounter;
      firePressed = firePressed
        || myEvent.get(Event::MouseButtonLeftValue);
    }
    if(myControlIDY > -1)
    {
      int m_axis = myEvent.get(Event::MouseAxisYMove);
      if(m_axis < -2)     --myCounter;
      else if(m_axis > 2) ++myCounter;
      firePressed = firePressed
        || myEvent.get(Event::MouseButtonRightValue);
    }
  }
  setPin(DigitalPin::Six, !getAutoFireState(firePressed));

  // Only consider the lower-most bits (corresponding to pins 1 & 2)
  myGrayIndex = Int32(myCounter * SENSITIVITY / 4.0F) & 0b11;

  // Stelladaptor is the only controller that should set this
  int yaxis = myEvent.get(myYAxisValue);

  // Only overwrite gray code when Stelladaptor input has changed
  // (that means real changes, not just analog signal jitter)
  if((yaxis < (myLastYaxis - 1024)) || (yaxis > (myLastYaxis + 1024)))
  {
    myLastYaxis = yaxis;
    if(yaxis <= -16384-4096)
      myGrayIndex = 3; // up
    else if(yaxis > 16384+4096)
      myGrayIndex = 1; // down
    else if(yaxis >= 16384-4096)
      myGrayIndex = 2; // up + down
    else /* if(yaxis < 16384-4096) */
      myGrayIndex = 0; // no movement

    // Make sure direct gray codes from Stelladaptor stay in sync with
    // simulated gray codes generated by PC keyboard or PC joystick
    myCounter = myGrayIndex / SENSITIVITY * 4.0F;
  }

  // Gray codes for rotation
  static constexpr std::array<uInt8, 4> graytable = { 0x03, 0x01, 0x00, 0x02 };

  // Determine which bits are set
  uInt8 gray = graytable[myGrayIndex];
  setPin(DigitalPin::One, (gray & 0x1) != 0);
  setPin(DigitalPin::Two, (gray & 0x2) != 0);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool Driving::setMouseControl(
    Controller::Type xtype, int xid, Controller::Type ytype, int yid)
{
  // When the mouse emulates a single driving controller, only the X-axis is
  // used, and both mouse buttons map to the same 'fire' event
  if(xtype == Controller::Type::Driving && ytype == Controller::Type::Driving && xid == yid)
  {
    myControlID = ((myJack == Jack::Left && xid == 0) ||
                   (myJack == Jack::Right && xid == 1)
                  ) ? xid : -1;
    myControlIDX = myControlIDY = -1;
  }
  else
  {
    // Otherwise, each axis can be mapped to a separate driving controller,
    // and the buttons map to separate (corresponding) controllers
    myControlID = -1;
    if(myJack == Jack::Left)
    {
      myControlIDX = (xtype == Controller::Type::Driving && xid == 0) ? 0 : -1;
      myControlIDY = (ytype == Controller::Type::Driving && yid == 0) ? 0 : -1;
    }
    else  // myJack == Right
    {
      myControlIDX = (xtype == Controller::Type::Driving && xid == 1) ? 1 : -1;
      myControlIDY = (ytype == Controller::Type::Driving && yid == 1) ? 1 : -1;
    }
  }

  return true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Driving::setSensitivity(int sensitivity)
{
  BSPF::clamp(sensitivity, MIN_SENSE, MAX_SENSE, (MIN_SENSE + MAX_SENSE) / 2);
  SENSITIVITY = sensitivity / 10.0F;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
float Driving::SENSITIVITY = 1.0;
