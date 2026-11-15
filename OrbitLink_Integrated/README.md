# OrbitLink_Integrated - Send/receive + XOR + Logger

`OrbitLink_App` (Cho: Senders/Receiver and the priority queue) with Shushant's XOR encryption and
Logger task adapted to the shared message format `orbit_msg_t`. `OrbitLink_App` and `OrbitLink_Queue`
are not modified by this folder.

## Run
```bash
cd OrbitLink_Integrated
make                    # Linux: make CC=gcc
./orbitlink_integrated
echo $?                 # 0 = all checks passed
```
Kernel setup is the same as in `OrbitLink_App/README.md`.

## What changed from Shushant's code
| Item | Shushant | Integrated | Why |
|---|---|---|---|
| Message struct | `Message` (NORMAL=0, EMERGENCY=1) | `orbit_msg_t` (EMERGENCY=0, NORMAL=2) | Same format as the queue module |
| XOR | `xor_crypt(char*, len, key)` | `orbit_xor_encrypt/decrypt(msg)` | Also manages the `encrypted` flag and prevents double encryption |
| Sending a log entry | Waits with `portMAX_DELAY` | No wait; if full, drop and count | A backed-up log must never stop a Sender |
| Printing logs | No critical section | Printed inside a critical section | Avoids printf deadlock in the POSIX port |
| Exit | `exit(0)` | `vTaskEndScheduler()` | Uses the normal shutdown path |
| Queue | One FIFO | Three priority queues | Uses Cho's queue module |

## Checks (8)
All received, per-type statistics, order within each type, encrypted while queued, decrypted text
matches the original, Emergency -> System -> Normal order, Emergency Sender has the highest task
priority (4, the others 3), no log lines lost.

## Not included yet
- Retry and backoff. Plug in with `mq_set_full_callback()` after `mq_init()` in `main()`.
  Example: `on_queue_full` and `retry_task` in `OrbitLink_Queue/src/main_demo.c`.
- Full-queue situations are not exercised in this version.
