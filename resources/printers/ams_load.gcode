M620 S[next_extruder]
M106 S255
M104 S250
M17 S
M17 X0.5 Y0.5
G91
G1 Y-5 F1200
G1 Z3
G90
G28 X
M17 R
G1 X70 F21000
G1 Y245
G1 Y265 F3000
G4
M106 S0
M109 S250
G1 X90
G1 Y255
G1 X120
G1 X20 Y50 F21000
G1 Y-3
T[next_extruder]
G1 X54
G1 Y265
G92 E0
G1 E40 F180
G4
M104 S[new_filament_temp]
G1 X70 F15000
G1 X76
G1 X65
G1 X76
G1 X65
G1 X90 F3000
G1 Y255
G1 X100
G1 Y265
G1 X70 F10000
G1 X100 F5000
G1 X70 F10000
G1 X100 F5000
G1 X165 F12000
G1 Y245
G1 X70
G1 Y265 F3000
G91
G1 Z-3 F1200
G90
M621 S[next_extruder]

