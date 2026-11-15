# OrbitLink_App - Basic send/receive app (Cho's part)

Three Senders (Emergency, System, Normal) and one Receiver exchange messages over the queue module
from `OrbitLink_Queue`, and the program checks automatically that priority handling works.
The queue module (`src/msg_queue.c`, `include/*.h`) has the same code as in `OrbitLink_Queue`.

## Run
```bash
cd OrbitLink_App
make          # macOS uses clang; on Linux use: make CC=gcc
./orbitlink_app
echo $?       # 0 = all checks passed, 1 = at least one failed
```
The kernel is taken from `FreeRTOS-Kernel/` inside this folder if it exists, otherwise from
`../FreeRTOS_시뮬레이터/FreeRTOS-Kernel`. To get the kernel yourself:
```bash
git clone https://github.com/FreeRTOS/FreeRTOS-Kernel.git
cd FreeRTOS-Kernel && git fetch --depth 1 origin ca759444e9ca59cfa3a9faa6cb8b49e0772ca520 && git checkout ca759444e9ca59cfa3a9faa6cb8b49e0772ca520 && cd ..
```

## What happens
1. Three Senders send 12 messages (E 3, S 3, N 6) with their arrival order mixed.
2. The Receiver starts at tick 300, after everything is queued, and takes messages out.
3. The Checker judges 6 items: all received, per-type statistics, order within each type,
   payload intact, Emergency -> System -> Normal dequeue order, and the Emergency Sender
   having the highest task priority (4, the others 3).

See `run_output.txt` for a real run.

## Emergency is favoured in two ways
| Mechanism | What it decides |
|---|---|
| Task priority (Emergency Sender 4, others 3) | Which Sender gets the CPU first when sends overlap |
| Dequeue order in the queue module | Which message the Receiver handles first |

Task priority alone cannot move an Emergency message ahead of Normal messages that are already
waiting in a queue, so both are used.

## Where the next owners plug in (see comments in src/app_main.c)
| Feature | Where |
|---|---|
| XOR encryption | In `sender_task()`, right before `mq_send()`; set `msg.encrypted = 1` |
| XOR decryption | In `receiver_task()`, right after `mq_receive()` |
| Retry | In `main()`, register `mq_set_full_callback()` after `mq_init()` (example: `on_queue_full` and `retry_task` in `OrbitLink_Queue/src/main_demo.c`) |
| Logger | Replace `vLog()` with calls to a logger task |

Notes. XOR output can contain 0x00, so never treat the payload as a C string (print it with a length, e.g. `%.*s`).
Do not call `printf` or `usleep` directly inside a task.

## Known limitations
- This is a small scenario where the queues never fill up. Full-queue behaviour is covered by scenarios 2 and 3 in `OrbitLink_Queue`.
- The priority check works by "start the Receiver late and take everything out after it is queued", so it does not measure latency when messages arrive mixed in real time.
- Build and run verified on macOS (Apple Silicon, clang) and Linux (gcc), 2026-11-15.
