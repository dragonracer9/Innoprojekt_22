#if defined(ARDUINO) && ARDUINO >= 100
#include "Arduino.h"
#endif

#ifndef Servo_h
#include <Servo.h>
#endif 

#include "PlotterV3.h"

#ifndef PLT_H
#error Plotter not defined
#endif // !PLT_h
#ifndef Servo_h
#error Servo not defined
#endif // !Servo_h

#ifndef PMTH
#include "pmath.h"
#endif // !PMTH
#ifndef ERRORS
#include "errors"
#endif // !ERRORS

//maths bc i dont want a seperate maths.cpp file
namespace pmath
{
    long cbez_x(long x, long c1_x, long c2_x, long end_x, uint8_t prec, uint8_t i)
    {
        double t = double(i) / prec;
        return (cube(1 - t)*x) + (sq(1 - t)*3*t*c1_x) + ((1 - t)*3*sq(t)*c2_x) + (cube(t)*end_x);
    }

    long cbez_y(long y, long c1_y, long c2_y, long end_y, uint8_t prec, uint8_t i)
    {
        double t = double(i) / prec;
        return (cube(1 - t)*y) + (sq(1 - t)*3*t*c1_y) + ((1 - t)*3*sq(t)*c2_y) + (cube(t)*end_y);
    }

    long qbez_x(long x, long c1, long end_x, uint8_t prec, uint8_t i)
    {
        double t = double(i) / prec;
        return (sq(1 - t)*x) + ((1 - t)*2*t*c1) + (sq(t)*end_x);
    }

    long qbez_y(long x, long c1, long end_x, uint8_t prec, uint8_t i)
    {
        double t = double(i) / prec;
        return (sq(1 - t)*x) + ((1 - t)*2*t*c1) + (sq(t)*end_x);
    }

    Vector Vector::orth() const 
    {
        return Vector(-y, x); // using the simple version -> vector is turned by 90° anticlockwise 
    }
    
    double Vector::norm() const
    {
        return sqrt(sq(x) + sq(y));
    }

    Vector Vector::rotate(double theta)
    {
        long temp_x = x;
        x = (x * cos(theta) - y * sin(theta));
        y = (temp_x * sin(theta) + y * cos(theta));
        return Vec(x,y);
    }

    Vector Vector::post_rotate(double theta)
    {
        Vec temp = Vec(x, y);

        x = (x * cos(theta) - y * sin(theta));
        y = (temp.x * sin(theta) + y * cos(theta));

        return temp;
    }

} // namespace pmath


Servo servo;

static inline void set_speed(const pin motor, int speed) { analogWrite(motor, speed); }
static inline void set_brakes(const pin motor, int state) { digitalWrite(motor, state); }
static void set_speed(const pin pins[3], int speed)
{
    if (speed == 0)
    {
        digitalWrite(pins[2], HIGH);
        analogWrite(pins[0], 0);

        return;
    }

    digitalWrite(pins[1], (speed < 0) ? LOW : HIGH);
    analogWrite(pins[0], abs(speed));

    digitalWrite(pins[2], LOW);
}

static void emergency_stop()
{
    /*Engage Brakes*/
    digitalWrite(_BRAKE_A, HIGH);
    digitalWrite(_BRAKE_B, HIGH);

    /*Cut Power to the motors*/
    analogWrite(_SPEED_A, 0);
    analogWrite(_SPEED_B, 0);

    abort();
}

Plotter::Plotter() 
{
    Serial.begin(9600);
    pinMode(_BRAKE_A, OUTPUT);
    pinMode(_BRAKE_B, OUTPUT);
    pinMode(_SPEED_A, OUTPUT);
    pinMode(_SPEED_B, OUTPUT);
    pinMode(_DIR_A, OUTPUT);
    pinMode(_DIR_B, OUTPUT);

    pinMode(_SWITCH, INPUT);

    servo.attach(_SERVO);

    pins_x[0] = _SPEED_A, pins_x[1] = _DIR_A, pins_x[2] = _BRAKE_A;
    pins_y[0] = _SPEED_B, pins_y[1] = _DIR_B, pins_y[2] = _BRAKE_B;
    noInterrupts();
    /* if (run_into_walls(pins_x, pins_y)){ //Switch if necessary
        pins_x[0] = _SPEED_B, pins_x[1] = _DIR_B, pins_x[2] = _BRAKE_B;
        pins_y[0] = _SPEED_A, pins_y[1] = _DIR_A, pins_y[2] = _BRAKE_A;
    } */
    interrupts();

    x = 0;
    y = 0;

    attachInterrupt(_SWITCH, emergency_stop, RISING); 
}

bool Plotter::is_active(){
    return on;
}

bool Plotter::run_into_walls(pin pins_x[3], pin pins_y[3])
{
    /*Make sure B is off*/
    set_speed(pins_y, 0);

    set_speed(pins_x, 255); //Run A forward
    /*Run until button*/
    while (digitalRead(_SWITCH) != HIGH) {} //Do Nothing
    /*Stop A*/
    set_speed(pins_x, 0);
    /*Start B*/
    set_speed(pins_y, 255);
    /*Run until button*/
    while (digitalRead(_SWITCH) == LOW) {} //Do Nothing
    /*Stop B*/
    set_speed(pins_y, 0);
    /*Start A*/
    set_speed(pins_x, -255);

    int time = millis();
    while (digitalRead(_SWITCH) == LOW) {} //Do Nothing
    int duration_x = millis()-time;

    /*Stop A*/
    set_speed(pins_x, 0);
    /*Start B*/
    set_speed(pins_y, -255);

    time = millis();
    while (digitalRead(_SWITCH) == LOW) {} //Do Nothing
    int duration_y = millis() - time;

    /*Stop B*/
    set_speed(pins_y, 0);

    return duration_x > duration_y;
}

bool Plotter::out_of_bounds(int dx, int dy)
{
    int new_x = x + dx;
    int new_y = y + dy;
    if (new_x < 0 || new_y < 0 || new_x > MAX_X || new_y > MAX_Y)
    {
        return true;
    }
    return false;
}

void Plotter::draw_line(long dx, long dy)
{

    if (dx == 0 && dy == 0)
    {
        set_speed(pins_x, 0);
        set_speed(pins_y, 0);
        return;
    }

    double norm = sqrt(sq(dx) + sq(dy));
    double n_dx = (dx / norm) * CORRECTION; //--> 0
    double n_dy = dy / norm; //--> 1

    bool x_geq = abs(n_dx) >= abs(n_dy); //--> false
    bool y_geq = abs(n_dy) >= abs(n_dx); //--> true

    set_speed(pins_x, ( (x_geq) ? 255 : int(speed_to_bits(n_dx/n_dy)) ) * sign(dx)); //--> sets speed to 0
    set_speed(pins_y, ( (y_geq) ? 255 : int(speed_to_bits(n_dy/n_dy)) ) * sign(dy)); //--> sets speed to -255

    int eta = millis() + int(abs( ( (x_geq) ? dx / MAX_SPEED_X : dy / MAX_SPEED_Y )));

    while (millis() < eta) {}

    set_speed(pins_x, 0);
    set_speed(pins_y, 0);

    x += dx;
    y += dy;
}


void Plotter::draw_line(const Vec & delta)
{
    draw_line(delta._x(), delta._y());
}


void Plotter::servo_angle(int value)
{
    servo.write(value);
    angle = value;
}

void Plotter::up()
{
    servo_angle(110);
}

void Plotter::down()
{
    servo_angle(170);
}

void Plotter::move(long dx, long dy)
{
    up();
    draw_line(dx, dy);
    down();
}

void Plotter::move(Vec & delta)
{
    up();
    draw_line(delta);
    down();
}

void Plotter::bezier_q(long c1_x, long c1_y, long end_x, long end_y, uint8_t precision)
{
    if (precision <= 0 || 50 < precision) {
        return;
    }
    long start_x = x, start_y = y;
    long p_x, p_y;

    for (uint8_t i = 0; i < precision; ++i)
    {
        p_x = pmath::qbez_x(start_x, c1_x, end_x, precision, i) - x;
        p_y = pmath::qbez_y(start_y, c1_y, end_y, precision, i) - y;

        draw_line(p_x, p_y);
    }
    return;
}

void Plotter::bezier_c(long c1_x, long c1_y, long c2_x, long c2_y, long end_x, long end_y, uint8_t precision)
{
    if (precision <= 0 || 50 < precision) {
        return;
    }
    long start_x = x, start_y = y;
    long p_x, p_y;

    for (uint8_t i = 0; i < precision; ++i)
    {
        p_x = pmath::cbez_x(start_x, c1_x, c2_x, end_x, precision, i) - x;
        p_y = pmath::cbez_y(start_y, c1_y, c2_y, end_y, precision, i) - y;

        draw_line(p_x, p_y);
    }
    return;
}

void Plotter::circle_seg(Vec & m, int radius, double max_angle = 2*PI, int precision = 20)
{
    { 
        double norm = m.norm(); //calculate norm first for efficionk
        draw_line( int( (m._x()-x)*(1-radius/norm) ), int( (m._y()-y)*(1-radius/norm) ) ); //draw to the radius
    }

  double da = 1.0/precision*sign(max_angle); //make sure the way is signed
  double agl = 0; //init

  Vec v = Vec(x, y) - m; 
  while(abs(agl) <= abs(max_angle))
  {
      Vec temp = v;
      draw_line(temp-v.rotate(da)); // rotate vector and subtract to get a dv
      agl += da; //keep track of angle
  }
}

void Plotter::spiral(Vec & mid)
{

    Vec start = mid - Vec(x,y);
    move(start);
    //spiral stuff with orth vecs

}