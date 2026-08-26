.. zephyr:code-sample:: ir_nec
   :name: Infrared (IR) using NEC protocol
   :relevant-api: ri

   Read data from IR received using NEC protocol.


Overview
********

This sample demonstrates how to read a sample data from an IR receiver using ir driver.

Remote controller/IR diode used in sample should send IR signal using NEC protocol.

Building and Running for QCC74x
*******************************
The IR peripheral and pinmux is configured in the board's ``.dts`` file.

Device Tree Configuration
------------------------
The sample uses GPIO10. For the qcc743m_evk and qcc744m_evk, this pin needs to be set with ``ir-enable`` option.
Also IR driver should have status "okay".

.. code-block:: dts
&pinctrl {
    status = "okay";
    ir_default: ir_default {
        group1 {
            pinmux = <GPIO10_SWGPIO10>;
			ir-enable;
            bias-pull-up;
            drive-strength = <1>;
            input-enable;
            input-schmitt-enable;
        };
    };
};

&ir {
	status = "okay";
	rx_mode = "IR_RX_NEC";
	input_inverse;
    pinctrl-0 = <&ir_default>;
    pinctrl-names = "default";
};

Running sample
--------------
Sample will wait for the signal aligned with NEC protocol.
OUT pin of IR receiver needs to be connected to GPIO10. Example receiver which can be used is TSOP94638.
After starting the sample, press any button in NEC IR controller to see the output.

Sample output
*************

.. code-block:: console

   IR data received: x00000000f30cff00

.. note:: Actual data depends on controller and command used. If the IR is not supported, the output will be an error message.
