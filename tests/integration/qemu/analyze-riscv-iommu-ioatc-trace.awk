#!/usr/bin/awk -f

BEGIN {
    FS = "[ =]"
    print "sample,reason,scans,entries,live,tombstones,updates,flushes"
}

/riscv_iommu_ioatc_stats/ {
    delete value
    reason = "unknown"

    for (i = 1; i <= NF; i++) {
        if ($i == "sample" || $i == "flush") {
            reason = $i
        } else if ($i == "scans" || $i == "entries" ||
                   $i == "live" || $i == "tombstones" ||
                   $i == "updates" || $i == "flushes") {
            value[$i] = $(i + 1) + 0
        }
    }

    samples++
    if (value["tombstones"] > maximum) {
        maximum = value["tombstones"]
    }
    if (samples == 1) {
        first = value["tombstones"]
    }
    last = value["tombstones"]

    printf "%d,%s,%d,%d,%d,%d,%d,%d\n", samples, reason,
           value["scans"], value["entries"], value["live"],
           value["tombstones"], value["updates"], value["flushes"]
}

END {
    if (!samples) {
        print "No riscv_iommu_ioatc_stats events found" > "/dev/stderr"
        exit 1
    }

    printf "samples=%d first_tombstones=%d last_tombstones=%d " \
           "max_tombstones=%d\n", samples, first, last, maximum > "/dev/stderr"
}
