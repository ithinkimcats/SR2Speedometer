# Saints Row 2 Speedometer
This adds a small speedometer to the game.

## Custom Speedometers:
* Add a `textures` folder in the same directory as the DLL/ASI file.
* Name and create textures with the following specifications:
* internal_name_background.png (256x256) - ex: car_4dr_police01_background.png
* internal_name_needle.png (15x94) - ex: car_4dr_police01_needle.png
* Create entry in `speedometer.ini`:
```
[internal_name]
needle_pivot_x=8
needle_pivot_y=100
needle_angle_min=225
needle_angle_max=510
speedometer_x=0.90
speedometer_y=0.80
max_speed=140
fade_speed = 2.5
```
Any missing values will be inherited from the default values.

## This project was developed with over 90% AI-generated code. I am not familiar with, nor use C++ actively.
