# Example configurations for Visual Studio Code

## Debugging with on-board CH347

1. Install CH347 driver in Windows: <https://www.wch-ic.com/downloads/CH343SER_ZIP.html>.
1. Copy or merge OpenOCD related configuration in `launch.json` and `tasks.json` into your project `.vscode` directory. Some paths to tools may need to be modified.
1. Build and flash, e.g. these commands in your West workarea:

    ```text
    west build -p -b qcc744m_evk .\zephyr\samples\basic\blinky\ -- -DCONFIG_NO_OPTIMIZATIONS=y
    west flash --port COM10
    ```

1. Click board RESET button to boot from flash.
1. Select "Run and Debug (Ctrl+Shift+D)" in Visual Studio Code.
    - Select "Debug with OpenOCD".
    - "Start Debugging (F5)".
    - Selec "Debug anyway".
    - "Pause (F5)".
    - In Visual Studio Code "Debug Console", use e.g. these commands to set a breakpoint in `main()` and start execution from `__start`:

        ```text
        -exec b main
        -exec jump __start
        ```

    - Execution should stop at breakpoint in `main()`.

### Notes

- There may be communication problems if QCC7xxM EVK has been running
more than half a minute. Resetting the EVK before starting debugging may help.
