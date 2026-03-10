#!/bin/bash
Xvfb :99 -screen 0 1280x720x24 &
sleep 1
DISPLAY=:99 glxgears &
tail -f /dev/null
