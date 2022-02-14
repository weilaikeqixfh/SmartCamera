Target=main
Object=main.c video.c motor.c


$(Target):$(Object)
	arm-linux-gcc $(Object) -o $(Target) -lpthread  -ljson -L install/lib -I install/include -lwiringPi -L WiringNP/wiringPi -I WiringNP/wiringPi

