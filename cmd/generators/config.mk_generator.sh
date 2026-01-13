#!/bin/bash
if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <replacement_string>"
    exit 1
fi
replacement_string="$1"
sed "s/template/$(echo $replacement_string | sed -e 's/[\/&]/\\&/g')/" ./tmpl/config.mk.tmpl > ./config.mk
echo "config.mk file has been generated with TARGET_MAC_ADDRESS=$replacement_string"