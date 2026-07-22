# Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
# SPDX-License-Identifier: BSD-3-Clause

#!/bin/bash

source $(realpath $(dirname $BASH_SOURCE))/../.init.test.sh

#ensure_wifi_connected

URL_HTTP="http://postman-echo.com/bytes"
URL_HTTPS="https://postman-echo.com/bytes"

# e.g. 'bshg' 'trigger' 'json_forever' '{"ll":[{"download":[{"url":"https://postman-echo.com/bytes/123456","dst":"discard"}]}]}'

remove_slash()
(
    echo $@ | sed 's@/@@g'
)

# set log level to inf for http_download
send_uart log enable inf http_download

for URL_BASE in $URL_HTTP $URL_HTTPS; do
    #############################
    # BIG file sizes with discard (= realistic throughput)
    #############################
    for size in 2/mb 1/mb; do
        sized=$(remove_slash $size)
        URL="$URL_BASE/$size"

        run_test_uart $(basename $BASH_SOURCE)+discard-$sized.csv --wait-max 30\
            bshg trigger json_forever '{"ll":[{"download":[{"url":"'$URL'","dst":"discard"}]}]}'
    done

    #############################
    # SMALL file sizes
    #############################
    for size in 100/kb 10/kb 1/b; do
        sized=$(remove_slash $size)
        URL="$URL_BASE/$size"

        #############################
        # discard (= throughput)
        #############################
        run_test_uart $(basename $BASH_SOURCE)+discard-$sized.csv --wait-max 30\
            bshg trigger json_forever '{"ll":[{"download":[{"url":"'$URL'","dst":"discard"}]}]}'

        #############################
        # flash
        #############################
        run_test_uart $(basename $BASH_SOURCE)+flash-$sized.csv --wait-max 30\
            bshg trigger json_forever '{"ll":[{"download":[{"url":"'$URL'","dst":"flash"}]}]}'

        #############################
        # littlefs
        #############################
        run_test_uart $(basename $BASH_SOURCE)+littlefs-$sized.csv --wait-max 30\
            bshg trigger json_forever '{"ll":[{"download":[{"url":"'$URL'","dst":"littlefs"}]}]}'
    done

done
