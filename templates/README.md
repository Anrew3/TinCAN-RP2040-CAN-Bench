# TinCAN Vehicle Templates

This folder contains vehicle template definitions for the TinCAN CAN-BUS Simulator.

## What are Templates?

Templates define how user actions (button presses, slider movements, etc.) map to CAN bus messages for specific vehicles. Instead of hardcoding CAN messages in firmware, templates allow you to:

- Add support for new vehicles without modifying firmware
- Share vehicle profiles with the community
- Customize existing templates for your specific needs

## Template Structure

Each template is a JSON file with the following sections:

```json
{
  "name": "Vehicle Name",
  "id": "unique_id",
  "version": "1.0.0",
  "author": "Your Name",
  "description": "Description of the template",

  "vehicle": {
    "make": "Ford",
    "model": "Mustang",
    "years": "2015-2023",
    "platform": "S550"
  },

  "buttons": { ... },
  "gauges": { ... },
  "temperature": { ... },
  "tpms": { ... },
  "blinkers": { ... },
  "vin": { ... },
  "background": [ ... ]
}
```

## Creating a New Template

1. **Start with a blank template**: Copy `_blank.json` and rename it
2. **Fill in vehicle info**: Name, make, model, years
3. **Map CAN messages**: Use a CAN bus analyzer to capture real messages from your vehicle
4. **Test thoroughly**: Verify each mapping works correctly
5. **Share**: Submit a pull request to add your template to the official repository

## Template Sections

### buttons
Maps steering wheel and console button presses to CAN messages.

```json
"buttons": {
  "canId": "0x81",
  "holdMs": 100,
  "intervalMs": 10,
  "default": "00 00 00 00 00 00 00 00",
  "commands": {
    "UP": "08 00 00 00 00 00 00 00",
    "DOWN": "01 00 00 00 00 00 00 00"
  }
}
```

### gauges
Maps gauge values (RPM, Speed) to CAN messages with value encoding.

```json
"gauges": {
  "RPM": {
    "canId": "0x204",
    "base": "00 00 00 00 00 00 00 00",
    "valueBytes": [3, 4],
    "scale": 0.5,
    "min": 0,
    "max": 8000,
    "intervalMs": 100
  }
}
```

### temperature
Maps temperature sensors with conversion formulas.

```json
"temperature": {
  "canId": "0x156",
  "base": "00 00 00 00 03 00 00 00",
  "sensors": {
    "COOLANT": {
      "byte": 0,
      "formula": "(value - 32) * 5 / 9 + 60"
    }
  }
}
```

### tpms
Maps tire pressure values with unit conversion.

```json
"tpms": {
  "canId": "0x3B5",
  "tires": {
    "FL": { "byte": 1, "scale": 6.895 },
    "FR": { "byte": 3, "scale": 6.895 }
  }
}
```

### blinkers
Maps turn signal states to CAN message bit manipulation.

```json
"blinkers": {
  "canId": "0x3B3",
  "base": "40 48 C0 10 10 00 00 02",
  "left": { "byte": 6, "mask": "0x40" },
  "right": { "byte": 4, "mask": "0x08" }
}
```

### background
Continuous messages sent to keep the cluster alive.

```json
"background": [
  {
    "canId": "0x109",
    "data": "00 03 01 00 00 00 00 28",
    "intervalMs": 10,
    "description": "Cluster keepalive"
  }
]
```

## Contributing Templates

We welcome contributions! To submit a new template:

1. Fork the repository
2. Create your template following the schema
3. Test thoroughly with real hardware
4. Submit a pull request with:
   - Your template JSON file
   - Description of the vehicle
   - Any notes about quirks or limitations

## Available Templates

| File | Vehicle | Years | Author |
|------|---------|-------|--------|
| `mustang_s550.json` | Ford Mustang S550 | 2015-2023 | TinCAN Community |
| `f150_13gen.json` | Ford F-150 13th Gen | 2015-2020 | TinCAN Community |

## Resources

- [CAN Bus Basics](https://en.wikipedia.org/wiki/CAN_bus)
- [Ford CAN Bus Research](https://forscan.org)
- [TinCAN GitHub Repository](https://github.com/Anrew3/TinCAN-RP2040-CAN-Bench)
