#!/bin/bash
if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <replacement_string>"
    exit 1
fi
replacement_string="$1"
sed "s/template/$(echo $replacement_string | sed -e 's/[\/&]/\\&/g')/" ./tmpl/machine-info.tmpl > machine-info
echo "machine-info file has been generated with PRETTY_HOSTNAME=$replacement_string"