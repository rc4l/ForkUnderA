# features/soundseam

The sound system's line between the game and the main thread. The game keeps its channel bookkeeping, so the
checks that ask whether a sound is still playing answer at once; what it asks of the audio device is recorded
and played on the main thread, which in a browser is the only thread that may reach the device.

## In-engine hooks

None yet.
