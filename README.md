# LAB4 - Cooperative Scheduler

This project implements the cooperative scheduler required by Lab 4 on an
STM32F103C6.

## Implemented requirements

- TIM2 generates one scheduler tick every 10 ms.
- `SCH_Update()` runs from the TIM2 interrupt callback.
- `SCH_Dispatch_Tasks()` runs callbacks cooperatively in the main loop.
- `SCH_Add_Task()` adds one-shot or periodic callbacks and returns a task ID.
- `SCH_Delete_Task()` removes a callback by task ID.
- A 256-slot timing wheel schedules the lab's 1-255 tick timeouts without
  scanning or sorting the complete task array.
- A free-list allocates and reuses task IDs in O(1), instead of searching the
  task array for an inactive entry.
- A FIFO ready queue preserves callback order when several tasks expire at the
  same time.
- Five demonstration tasks run every 500, 1000, 1500, 2000, and 2500 ms.
- An additional callback toggles PA5 every 10 ms for measurement with a logic
  analyzer or oscilloscope.

## LED demonstration

The five scheduled tasks toggle these outputs:

| Output | Task interval |
| --- | ---: |
| PA0 (LED1) | 500 ms |
| PA1 (LED2) | 1000 ms |
| PA2 (LED3) | 1500 ms |
| PA3 (LED4) | 2000 ms |
| PA4 (LED5) | 2500 ms |

PA5 changes state every 10 ms. This produces a 50 Hz square wave because one
complete high-low cycle takes 20 ms.

Connect each LED from its GPIO through a 220-330 ohm resistor to ground. Connect
PA5 to a logic-analyzer or oscilloscope input.

## Virtual Terminal

USART1 provides optional status output at 38400 baud, 8 data bits, no parity,
and 1 stop bit. Connect PA9 (USART1 TX) to the Virtual Terminal RXD input. PA10
(USART1 RX) may be connected to TXD but is not required for output-only use.

The terminal prints a scheduler status line every 100 ms and reports whenever
one of the five LED tasks runs.

## Build result

The Debug configuration builds with zero errors and zero warnings. Flash
`Debug/LAB4.hex` or `Debug/LAB4.bin`.

The firmware has been compilation-checked, but the real board or Proteus
simulation still needs to be run to confirm the external wiring.
