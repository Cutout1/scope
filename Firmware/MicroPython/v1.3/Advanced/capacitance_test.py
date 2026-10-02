from machine import Timer, Pin, PWM, ADC, SPI
import time
import data
from ST7735 import TFT
from sysfont import sysfont

C1Ref = ADC(29)
C1 = ADC(28)
C2Ref = ADC(27)
C2 = ADC(26)

c1gnd = Pin(25, Pin.OUT)
c1gnd.value(0)
c2gnd = Pin(24, Pin.OUT)
c2gnd.value(0)
c3gnd = Pin(23, Pin.OUT)
c3gnd.value(0)
c4gnd = Pin(22, Pin.OUT)
c4gnd.value(0)

thev1k = Pin(4, Pin.OUT)
thev1k.value(1)
thev10k = Pin(5, Pin.OUT)
thev10k.value(1)
thev100k = Pin(6, Pin.OUT)
thev100k.value(1)
thev1M = Pin(7, Pin.OUT)
thev1M.value(1)

target = 0.632 * 65535 / 2 + 65535 / 2

initial_time = time.ticks_ms()
thev1M.value(0)
while C1.read_u16() < target:
    pass
final_time = time.ticks_ms()
thev1M.value(1)

tau = time.ticks_diff(final_time, initial_time)
print(tau/1000000000)
    