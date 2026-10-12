# Bus and device model

VART uses a small C-level bus/device layer instead of a full object model.
`VartBus` owns the list of attached devices and the address space used by that
bus.  `VartDevice` carries common lifecycle state, its parent bus, and the DMA
address space used by the device.

Devices are attached before they are realized.  Realization is transactional:
an operation failure leaves the device unrealized, so callers can remove it
without a partial lifecycle state.  A realized device must be unrealized before
it is removed from a bus, and a bus must be empty before it is destroyed.

The common layer does not impose PCIe-specific behavior.  PCIe devices will
extend `VartDevice` with configuration space, BARs, and MSI-X state, while
system-bus devices can use the same lifecycle without carrying PCIe fields.
