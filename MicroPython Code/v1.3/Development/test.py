from machine import Pin, PWM, mem32, ADC
from array import array
from uctypes import addressof
import time

result_array = array('H', (0 for _ in range(16)))

# ADC REGISTER SET UP

ADC_BASE = 0x4004c000

ADC_CS = ADC_BASE + 0x00
ADC_FCS = ADC_BASE + 0x08
ADC_FIFO = ADC_BASE + 0x0c
ADC_DIV = ADC_BASE + 0x10

ADC_FCS_EN = 1
ADC_FCS_SHIFT = 0
ADC_FCS_ERR = 0
ADC_FCS_DREQ_EN = 1
ADC_FCS_UNDER = 1
ADC_FCS_OVER = 1
ADC_FCS_THRESH = 1
ADC_FCS_val = ADC_FCS_EN | (ADC_FCS_SHIFT<<1) | (ADC_FCS_ERR<<2) | (ADC_FCS_DREQ_EN<<3) | (ADC_FCS_UNDER<<10) | (ADC_FCS_OVER<<11) | (ADC_FCS_THRESH<<24)

ADC_CS_EN = 1
ADC_CS_TS_EN = 0
ADC_CS_START_ONCE = 0
ADC_CS_START_MANY = 1
ADC_CS_AINSEL = 0
ADC_CS_RROBIN = 0b01111
ADC_CS_val = ADC_CS_EN | (ADC_CS_TS_EN<<1) | (ADC_CS_START_ONCE<<2) | (ADC_CS_START_MANY<<3) | (ADC_CS_AINSEL<<12) | (ADC_CS_RROBIN<<16)

# DMA REGISTER SET UP
# define DMA register addresses
DMA_BASE = 0x50000000

CH4_READ_ADDR   = DMA_BASE+0x100
CH4_WRITE_ADDR  = DMA_BASE+0x104
CH4_TRANS_COUNT = DMA_BASE+0x108
CH4_CTRL_TRIG   = DMA_BASE+0x10c

CH5_READ_ADDR   = DMA_BASE+0x140
CH5_WRITE_ADDR  = DMA_BASE+0x144
CH5_TRANS_COUNT = DMA_BASE+0x148
CH5_CTRL_TRIG   = DMA_BASE+0x14c

CH6_READ_ADDR   = DMA_BASE+0x180
CH6_WRITE_ADDR  = DMA_BASE+0x184
CH6_TRANS_COUNT = DMA_BASE+0x188
CH6_CTRL_TRIG   = DMA_BASE+0x18c

DMA_TIMER1 = DMA_BASE + 0x424

# disable DMA channels before configuring
mem32[CH4_CTRL_TRIG] = 0
mem32[CH5_CTRL_TRIG] = 0
mem32[CH6_CTRL_TRIG] = 0

# configure channel 4 to pull 4 values from ADC when triggered
mem32[CH4_READ_ADDR] = ADC_FIFO
mem32[CH4_WRITE_ADDR] = addressof(result_array)
mem32[CH4_TRANS_COUNT] = 4

IRQ_QUIET=1 # do not generate an interrupt
TREQ_SEL=36   # adc dreq
CHAIN_TO=4    # no chaining for this test
RING_SEL=0
RING_SIZE=0   # no wrapping
INCR_WRITE=1  # increment through write array
INCR_READ=0   # single read location
DATA_SIZE=1   # 16-bit half word transfer
HIGH_PRIORITY=1
EN=1
mem32[CH4_CTRL_TRIG] = (IRQ_QUIET<<21)|(TREQ_SEL<<15)|(CHAIN_TO<<11)|(RING_SEL<<10)|(RING_SIZE<<9)|(INCR_WRITE<<5)|(INCR_READ<<4)|(DATA_SIZE<<2)|(HIGH_PRIORITY<<1)|(EN<<0)

time.sleep(0.1)
print(result_array)
print(bin(mem32[CH4_CTRL_TRIG]))

mem32[ADC_DIV] = 0
mem32[ADC_FCS] = ADC_FCS_val
mem32[ADC_CS] = ADC_CS_val

time.sleep(0.1)
print(result_array)
print(bin(mem32[CH4_CTRL_TRIG]))