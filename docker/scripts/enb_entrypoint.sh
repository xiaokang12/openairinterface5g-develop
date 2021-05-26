#!/bin/bash

set -euo pipefail

# Based another env var, pick one template to use
if [[ -v USE_FDD_CU ]]; then ln -s /opt/oai-enb/etc/cu.fdd.conf /opt/oai-enb/etc/enb.conf; fi
if [[ -v USE_FDD_DU ]]; then ln -s /opt/oai-enb/etc/du.fdd.conf /opt/oai-enb/etc/enb.conf; fi
if [[ -v USE_FDD_MONO ]]; then ln -s /opt/oai-enb/etc/enb.fdd.conf /opt/oai-enb/etc/enb.conf; fi
if [[ -v USE_TDD_MONO ]]; then ln -s /opt/oai-enb/etc/enb.tdd.conf /opt/oai-enb/etc/enb.conf; fi
if [[ -v USE_FDD_RCC ]]; then ln -s /opt/oai-enb/etc/rcc.if4p5.enb.fdd.conf /opt/oai-enb/etc/enb.conf; fi
if [[ -v USE_FDD_RRU ]]; then ln -s /opt/oai-enb/etc/rru.fdd.conf /opt/oai-enb/etc/enb.conf; fi
if [[ -v USE_TDD_RRU ]]; then ln -s /opt/oai-enb/etc/rru.tdd.conf /opt/oai-enb/etc/enb.conf; fi

# Only this template will be manipulated
CONFIG_FILES=`ls /opt/oai-enb/etc/enb.conf`

for c in ${CONFIG_FILES}; do
    # grep variable names (format: ${VAR}) from template to be rendered
    VARS=$(grep -oP '@[a-zA-Z0-9_]+@' ${c} | sort | uniq | xargs)

    # create sed expressions for substituting each occurrence of ${VAR}
    # with the value of the environment variable "VAR"
    EXPRESSIONS=""
    for v in ${VARS}; do
        NEW_VAR=`echo $v | sed -e "s#@##g"`
        if [[ "${!NEW_VAR}x" == "x" ]]; then
            echo "Error: Environment variable '${NEW_VAR}' is not set." \
                "Config file '$(basename $c)' requires all of $VARS."
            exit 1
        fi
        EXPRESSIONS="${EXPRESSIONS};s|${v}|${!NEW_VAR}|g"
    done
    EXPRESSIONS="${EXPRESSIONS#';'}"

    # render template and inline replace config file
    sed -i "${EXPRESSIONS}" ${c}
done

# Load the USRP binaries
if [[ -v USE_B2XX ]]; then
    /usr/lib/uhd/utils/uhd_images_downloader.py -t b2xx
elif [[ -v USE_X3XX ]]; then
    /usr/lib/uhd/utils/uhd_images_downloader.py -t x3xx
elif [[ -v USE_N3XX ]]; then
    /usr/lib/uhd/utils/uhd_images_downloader.py -t n3xx
fi

echo "=================================="
echo "== Starting eNB soft modem"
if [[ -v USE_ADDITIONAL_OPTIONS ]]; then
    echo "Additional option(s): ${USE_ADDITIONAL_OPTIONS}"
    new_args=()
    while [[ $# -gt 0 ]]; do
        new_args+=("$1")
        shift
    done
    for word in ${USE_ADDITIONAL_OPTIONS}; do
        new_args+=("$word")
    done
    echo "${new_args[@]}"
    exec "${new_args[@]}"
else
    echo "$@"
    exec "$@"
fi
