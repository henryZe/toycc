#!/usr/bin/python3
import re
import sys

for path in sys.argv[1:]:
    with open(path) as file:
        s = file.read()

        s = re.sub(r'\t\t\t\t.kind =', '', s)
        s = re.sub(r'\t\t\t\t.size =', '', s)
        s = re.sub(r'\t\t\t\t.align =', '', s)
        s = re.sub(r'\t\t\t\t.is_unsigned =', '', s)

        print(s)
