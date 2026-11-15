#!/usr/bin/env python3
"""Generate upstream floating-point modem filters, and modern MSVC headers."""
from pathlib import Path
import subprocess
import sys

source,include=map(Path,sys.argv[1:3]);generator=sys.argv[3]
for stem,modem,direction in (
    ('v17_v32bis_rx','V.17','-r'),('v17_v32bis_tx','V.17','-t'),
    ('v27ter_rx_2400','V.27ter2400','-r'),('v27ter_rx_4800','V.27ter4800','-r'),
    ('v27ter_tx_2400','V.27ter2400','-t'),('v27ter_tx_4800','V.27ter4800','-t'),
    ('v29rx','V.29','-r'),('v29tx','V.29','-t')):
    with (include/(stem+'_floating_rrc.h')).open('wb') as output:
        subprocess.run([generator,'-m',modem,direction],stdout=output,check=True)
header=(source/'spandsp.h.in').read_text()
for name,value in {'SPANDSP_USE_FIXED_POINT':'','SPANDSP_MISALIGNED_ACCESS_FAILS':'',
    'SPANDSP_USE_EXPORT_CAPABILITY':'#define SPANDSP_USE_EXPORT_CAPABILITY 1',
    'INSERT_INTTYPES_HEADER':'#include <stdint.h>','INSERT_MATH_HEADER':'#include <math.h>',
    'INSERT_STDBOOL_HEADER':'#ifndef __cplusplus\n#include <stdbool.h>\n#endif'}.items():
    header=header.replace('@'+name+'@',value)
(include/'spandsp.h').write_text(header)
if sys.platform=='win32':
    (include/'spandsp.h').write_text('#if defined(_MSC_VER) && !defined(__inline__)\n#define __inline__ __inline\n#endif\n'+header)
fast=(source/'spandsp/fast_convert.h').read_text()
block=fast.index('#elif defined(_M_X64)')
start=fast.find('    __inline long int lrint(double x)',block)
end=fast.index('    __inline long int lfastrint(double x)',block)
if start>=0 and start<end:
    fast=fast[:start]+'    /* Modern MSVC provides C99 lrint/lrintf in math.h. */\n'+fast[end:]
(include/'spandsp').mkdir(exist_ok=True)
(include/'spandsp/fast_convert.h').write_text(fast)
(source/'spandsp/fast_convert.h').write_text(fast)
telephony=source/'spandsp/telephony.h'
text=telephony.read_text()
if '#if defined(SPANDSP_STATIC)' not in text:
    text=text.replace('#if defined(_M_IX86)  ||  defined(_M_X64)',
        '#if defined(SPANDSP_STATIC)\n#define SPAN_DECLARE(type) type\n#define SPAN_DECLARE_NONSTD(type) type\n#define SPAN_DECLARE_DATA\n#elif defined(_M_IX86)  ||  defined(_M_X64)')
    telephony.write_text(text)
