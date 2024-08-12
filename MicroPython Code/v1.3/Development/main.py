from machine import Pin, ADC, SPI, PWM, Timer, mem32
from array import array
from uctypes import addressof
import time
import data
from ST7735 import TFT
from sysfont import sysfont

# define constants
VOLTAGE_FACTOR = 3.35/32768
PLOTTING_SCALE_FACTOR = 16

# define DMA register addresses
DMA_BASE = 0x50000000

CH2_READ_ADDR   = DMA_BASE+0x080
CH2_WRITE_ADDR  = DMA_BASE+0x084
CH2_TRANS_COUNT = DMA_BASE+0x088
CH2_CTRL_TRIG   = DMA_BASE+0x08c

CH3_READ_ADDR   = DMA_BASE+0x0c0
CH3_WRITE_ADDR  = DMA_BASE+0x0c4
CH3_TRANS_COUNT = DMA_BASE+0x0c8
CH3_CTRL_TRIG   = DMA_BASE+0x0cc

DMA_TIMER0 = DMA_BASE + 0x420

# define PWM register addresses

PWM_BASE = 0x40050000

CH2_DIV = PWM_BASE + 0x2c
CH2_CC = PWM_BASE + 0x34
CH2_TOP = PWM_BASE + 0x38

# set up display SPI interface and initialize an instance of the TFT display using the ST7735 library
tft_spi = SPI(1, baudrate=133000000, polarity=0, phase=0, sck=Pin(14), mosi=Pin(15))
tft=TFT(tft_spi,10,11,13)
tft.initr()
tft.rgb(True)
tft.rotation(3)
tft.fill(TFT.BLACK)
    
# initialize waveform output pin with PWM
wave_pin = Pin(20, mode=Pin.OUT)
wave_dac = PWM(wave_pin)
wave_dac.freq(100000)
wave_dac.duty_u16(32768)

mem32[CH2_TOP] = 254
mem32[CH2_CC] = 128

# set up scope analog inputs
# Scopocket uses differential inputs, so the measured voltage of channel 1 = c1 - c1_ref
c1_ref = ADC(29)
c1 = ADC(28)
c2_ref = ADC(27)
c2 = ADC(26)

# set up ground pins for each channel
# these pins are connected to the scope inputs through 10MΩ resistors
# the main point is so if the pins are left floating they read 0V, but this can be disabled by configuring these pins as high impedance inputs
c1_ref_gnd = Pin(25, Pin.OUT)
c1_ref_gnd.value(0)
c1_gnd = Pin(24, Pin.OUT)
c1_gnd.value(0)
c2_ref_gnd = Pin(23, Pin.OUT)
c2_ref_gnd.value(0)
c2_gnd = Pin(22, Pin.OUT)
c2_gnd.value(0)

# set up pins used to connect a series of resistors to c1 for resistance measurement
# setting each pin high means none of the resistors are connected to c1
thev_1k = Pin(4, Pin.OUT)
thev_1k.value(1)
thev_10k = Pin(5, Pin.OUT)
thev_10k.value(1)
thev_100k = Pin(6, Pin.OUT)
thev_100k.value(1)
thev_1M = Pin(7, Pin.OUT)
thev_1M.value(1)

# set up pins for the user input buttons and switch
left_button = Pin(0, Pin.IN, Pin.PULL_UP)
right_button = Pin(1, Pin.IN, Pin.PULL_UP)
mode_button = Pin(2, Pin.IN, Pin.PULL_UP)
select_button = Pin(3, Pin.IN, Pin.PULL_UP)
start_switch = Pin(9, Pin.IN, Pin.PULL_UP)
ready_switch = Pin(8, Pin.IN, Pin.PULL_UP)

# set up pin to detect if the battery voltage is low
low_batt = Pin(21, Pin.IN)

# set up waveform output and measurement arrays
dac_array = data.sine_wave()
dac_bytes = bytearray(8000)

for x in range(2000):
    dac_bytes[4*x + 3] = 0
    dac_bytes[4*x + 2] = 0
    dac_bytes[4*x + 1] = 0 #sine[x] >> 8
    dac_bytes[4*x + 0] = dac_array[x]

c1_measurement_array = [0] * 160
c2_measurement_array = [0] * 160
measurement_array_index = 0
c1_plotted_voltage_array = [0] * 160
c2_plotted_voltage_array = [0] * 160
cursor_pos = 80

#2-channel chained DMA for DAC output. channel 2 does the transfer, channel 3 reconfigures
p=array('I',[0]) #global 1-element array
def dac_dma(ar,nword):
    #first disable the DMAs to prevent corruption while writing
    mem32[CH2_CTRL_TRIG]=0
    mem32[CH3_CTRL_TRIG]=0
    #setup first DMA which does the actual transfer
    mem32[CH2_READ_ADDR]=addressof(ar)
    mem32[CH2_WRITE_ADDR]=CH2_CC
    mem32[CH2_TRANS_COUNT]=nword
    mem32[DMA_TIMER0] = 0x0001F424 # divide system clock by 62500 = 2000 samples per second
    IRQ_QUIET=0x1 #do not generate an interrupt
    TREQ_SEL=0x3b #timer 0 triggered
    CHAIN_TO=3    #start channel 3 when done
    RING_SEL=0
    RING_SIZE=0   #no wrapping
    INCR_WRITE=0  #for write to array
    INCR_READ=1   #for read from array
    DATA_SIZE=2   #32-bit word transfer
    HIGH_PRIORITY=1
    EN=1
    CTRL2=(IRQ_QUIET<<21)|(TREQ_SEL<<15)|(CHAIN_TO<<11)|(RING_SEL<<10)|(RING_SIZE<<9)|(INCR_WRITE<<5)|(INCR_READ<<4)|(DATA_SIZE<<2)|(HIGH_PRIORITY<<1)|(EN<<0)
    mem32[CH2_CTRL_TRIG]=CTRL2
    #setup second DMA which reconfigures the first channel
    p[0]=addressof(ar)
    mem32[CH3_READ_ADDR]=addressof(p)
    mem32[CH3_WRITE_ADDR]=CH2_READ_ADDR
    mem32[CH3_TRANS_COUNT]=1
    IRQ_QUIET=0x1 #do not generate an interrupt
    TREQ_SEL=0x3f #no pacing
    CHAIN_TO=2    #start channel 2 when done
    RING_SEL=0
    RING_SIZE=0   #no wrapping
    INCR_WRITE=0  #single write
    INCR_READ=0   #single read
    DATA_SIZE=2   #32-bit word transfer
    HIGH_PRIORITY=1
    EN=1
    CTRL3=(IRQ_QUIET<<21)|(TREQ_SEL<<15)|(CHAIN_TO<<11)|(RING_SEL<<10)|(RING_SIZE<<9)|(INCR_WRITE<<5)|(INCR_READ<<4)|(DATA_SIZE<<2)|(HIGH_PRIORITY<<1)|(EN<<0)
    mem32[CH3_CTRL_TRIG]=CTRL3

# Interrupt handler for ADC inputs
def measurement_handler(timer):
    global c1_measurement_array, c2_measurement_array, measurement_array_index
    c1_measurement_array[measurement_array_index] = c1.read_u16() - c1_ref.read_u16()
    c2_measurement_array[measurement_array_index] = c2.read_u16() - c2_ref.read_u16()
    if measurement_array_index == 159:
        measurement_array_index = 0
    else:
        measurement_array_index += 1


# begin waveform output
dac_dma(dac_bytes, 2000)

# begin measurement timer
measurement_timer = Timer(mode=Timer.PERIODIC, period=10, callback=measurement_handler)

def draw_axes():
    tft.text((85, 10), 'v', TFT.YELLOW, sysfont, 1, nowrap=True)
    tft.text((155, 60), 's', TFT.YELLOW, sysfont, 1, nowrap=True)
    
    tft.vline((79,15), 113, TFT.YELLOW)
    
    for x in range(15):
        tft.hline((78, 15 + 8*x), 3, TFT.YELLOW)

    tft.hline((0,71), 159, TFT.YELLOW)

    for x in range(15):
        tft.vline((9 + 10*x,70), 3, TFT.YELLOW)

def update_text():
    global tft
    tft.text((0, 0), '{:.2f}s'.format((cursor_pos-80)*0.01), TFT.GREEN, sysfont, 1, nowrap=True)

    tft.text((40, 0), '{:+.2f}V'.format(c1_plotted_voltage_array[cursor_pos]), TFT.RED, sysfont, 1, nowrap=True)
    tft.text((90, 0), '{:+.2f}V'.format(c2_plotted_voltage_array[cursor_pos]), TFT.BLUE, sysfont, 1, nowrap=True)

    tft.text((140, 0), 'SIN', TFT.YELLOW, sysfont, 1, nowrap=True)

def plot():
    global tft, plotting, c1_plotted_voltage_array, c2_plotted_voltage_array
    plotting_index = 0
    tft.fill(TFT.BLACK)
    draw_axes()
    while plotting_index < 160:
        while (plotting_index + initial_measurement_array_index - 80) % 160 == measurement_array_index:
            pass
        c1_plotted_voltage_array[plotting_index] = c1_measurement_array[(initial_measurement_array_index + plotting_index - 80) % 160] * VOLTAGE_FACTOR
        c2_plotted_voltage_array[plotting_index] = c2_measurement_array[(initial_measurement_array_index + plotting_index - 80) % 160] * VOLTAGE_FACTOR
        tft.pixel((plotting_index, 71 - int(c1_plotted_voltage_array[plotting_index]*PLOTTING_SCALE_FACTOR)),TFT.RED)
        tft.pixel((plotting_index, 71 - int(c2_plotted_voltage_array[plotting_index]*PLOTTING_SCALE_FACTOR)),TFT.BLUE)
        plotting_index += 1
    plotting = False

draw_axes()

def left_button_handler(pin):
    print("left clicked")

def right_button_handler(pin):
    print("right clicked")

def mode_button_handler(pin):
    print("mode clicked")

def select_button_handler(pin):
    print("select clicked")

def start_switch_handler(pin):
    global plotting, initial_measurement_array_index
    if not plotting:
        initial_measurement_array_index = measurement_array_index
        plotting = True
'''
def ready_switch_handler(pin):
    print("ready")
'''
plotting = False

left_button.irq(trigger=Pin.IRQ_FALLING, handler=left_button_handler)
right_button.irq(trigger=Pin.IRQ_FALLING, handler=right_button_handler)
mode_button.irq(trigger=Pin.IRQ_FALLING, handler=mode_button_handler)
select_button.irq(trigger=Pin.IRQ_FALLING, handler=select_button_handler)
start_switch.irq(trigger=Pin.IRQ_FALLING, handler=start_switch_handler)
#ready_switch.irq(trigger=Pin.IRQ_FALLING, handler=ready_switch_handler)

while True:
    if plotting:
        plot()
    #update_text()

