#ifdef HOMEPLATE_FASTEPD_IMAGES
#if !defined(ARDUINO_INKPLATE10V2)
#error "FastEPD image refresh is only supported on Inkplate 10 v2"
#endif

#include <FastEPD.h>
#include "homeplate.h"
#include "gray4.h"

// These IO entry points belong to the pinned FastEPD revision. They let its
// custom panel share Inkplate's PMIC and expander objects, rather than resetting
// Wire or keeping a second, conflicting cache of the expander registers.
extern int bbepIOInit(FASTEPDSTATE *state);
extern void bbepIODeInit(void);

static_assert(CLEAR_NONE == 0 && BBEP_SUCCESS == 0, "Update gray4.h for this FastEPD API");

static FASTEPD imagePanel;
static bool attempted = false;
static bool ready = false;

static int panelIO(void *) {
    // Inkplate::einkOn() sets up the board's GPIOs before the LCD bus attaches.
    return BBEP_SUCCESS;
}

static int panelPower(void *context, int on) {
    auto *state = static_cast<FASTEPDSTATE *>(context);
    if (on) {
        // Use the board's existing power sequencing and factory VCOM. FastEPD
        // attaches the parallel bus after einkOn has configured GPIO output.
        if (!display.einkOn()) {
            return BBEP_IO_ERROR;
        }
        int result = bbepIOInit(state);
        if (result != BBEP_SUCCESS) {
            bbepIODeInit();
            display.einkOff();
            return result;
        }
        state->pwr_on = 1;
    } else {
        // Release the LCD peripheral before Inkplate puts the pins into high
        // impedance. Its normal text/config screens can then use GPIO again.
        bbepIODeInit();
        display.einkOff();
        state->pwr_on = 0;
    }
    return BBEP_SUCCESS;
}

// Inkplate row timing from FastEPD's Inkplate5V2RowControl (Apache-2.0,
// Copyright BitBank Software, Inc.). Use Inkplate's shared expander cache.
static void panelRow(void *, int mode) {
    if (mode == ROW_START) {
        digitalWrite(32, HIGH);
        delayMicroseconds(7);
        display.expander1.digitalWrite(SPV, LOW, true);
        delayMicroseconds(10);
        digitalWrite(32, LOW);
        digitalWrite(32, HIGH);
        delayMicroseconds(8);
        display.expander1.digitalWrite(SPV, HIGH, true);
        delayMicroseconds(10);
        digitalWrite(32, LOW);
        digitalWrite(32, HIGH);
        delayMicroseconds(18);
        digitalWrite(32, LOW);
        digitalWrite(32, HIGH);
        delayMicroseconds(18);
        digitalWrite(32, LOW);
        digitalWrite(32, HIGH);
    } else if (mode == ROW_STEP) {
        digitalWrite(32, LOW);
        digitalWrite(2, HIGH);
        digitalWrite(2, LOW);
    }
}

static bool initializePanel() {
    if (!attempted) {
        attempted = true;
        // The 2-bit update path has its own pixel pushes. A 16-gray matrix is
        // unused, but FastEPD requires a valid one when allocating its buffers.
        static const uint8_t unusedGrayMatrix[16] = {};
        BBPANELDEF definition = {};
        definition.width = E_INK_WIDTH;
        definition.height = E_INK_HEIGHT;
        definition.bus_speed = 13333333;
        definition.flags = BB_PANEL_FLAG_SLOW_SPH | BB_PANEL_FLAG_MIRROR_X | BB_PANEL_FLAG_MIRROR_Y;
        const uint8_t dataPins[] = {4, 5, 18, 19, 23, 25, 26, 27};
        memcpy(definition.data, dataPins, sizeof(dataPins));
        definition.bus_width = 8;
        definition.ioCKV = 32;
        definition.ioSPH = 33;
        definition.ioLE = 2;
        definition.ioCL = 0;
        definition.ioDCDummy = 15;
        definition.iLinePadding = 16;
        definition.pGrayMatrix = unusedGrayMatrix;
        definition.iMatrixSize = sizeof(unusedGrayMatrix);
        BBPANELPROCS callbacks = {panelPower, panelIO, panelRow, nullptr, nullptr};
        ready = imagePanel.initCustomPanel(&definition, &callbacks) == BBEP_SUCCESS &&
                imagePanel.setPanelSize(E_INK_WIDTH, E_INK_HEIGHT, definition.flags) == BBEP_SUCCESS &&
                imagePanel.setMode(BB_MODE_2BPP) == BBEP_SUCCESS;
        Serial.printf("[IMAGE] FastEPD four-gray backend: %s\n", ready ? "ready" : "initialization failed");
    }
    return ready;
}

bool displayImageNoClear(bool decoded) {
    if (!decoded || !initializePanel()) {
        return false;
    }
    invalidateImageCache();
    bool good = refreshGray4Image(imagePanel, display.DMemory4Bit,
                                  E_INK_WIDTH * E_INK_HEIGHT / 2,
                                  E_INK_WIDTH, E_INK_HEIGHT, decoded);
    Serial.printf("[IMAGE] FastEPD full image, CLEAR_NONE: %s\n", good ? "done" : "failed");
    return good;
}
#endif
