-- DESCRIPTION: Verilator: VHDL physical types with user-declared units
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- Every value here is the one GHDL and nvc report for the same expression
package phys is
  type FREQ is range 0 to integer'high units
    Hz;
    kHz = 1000 Hz;
    MHz = 1000 kHz;
  end units;
  type BAUD is range 0 to integer'high units
    Bd;
    kBd = 1000 Bd;
    MBd = 1000 kBd;
  end units;
  type MEMORY is range 0 to integer'high units
    Byte;
    KiB = 1024 Byte;
    MiB = 1024 KiB;
  end units;
  constant C_1MHZ : FREQ := 1 MHz;
  constant C_SUM : FREQ := 2 MHz + 500 kHz;
  constant C_FRAC : FREQ := 1.5 kHz;
  constant C_1MIB : MEMORY := 1 MiB;
  constant C_3MIB : MEMORY := 3 MiB;
  constant C_1MBD : BAUD := 1 MBd;
end package;

library work;
use work.phys.all;

entity t is
  generic (
    CLOCK_FREQ : FREQ := 100 MHz;
    SYMBOL_RATE : BAUD := 115200 kBd
  );
  port (
    q_step : out integer;  -- 1 MHz / 1 Hz, the unit that sets the step
    q_gen_k : out integer;  -- the 100 MHz generic / 1 kHz
    q_gen_hz : out integer;  -- the 100 MHz generic / 1 Hz
    q_baud : out integer;  -- the 115200 kBd generic / 1 kBd
    q_sum : out integer;  -- (2 MHz + 500 kHz) / 1 Hz
    q_div : out integer;  -- 1 MHz / 2 kHz
    q_frac : out integer;  -- 1.5 kHz / 1 Hz, a fractional abstract literal
    q_kib : out integer;  -- 1 MiB / 1 KiB
    q_mib : out integer;  -- 1 MiB / 1 Byte, the step of the MEMORY type
    q_mib3 : out integer;  -- 3 MiB / 1 Byte, two steps of 1024
    q_mbd : out integer  -- 1 MBd / 1 Bd, a chain of three units
  );
end entity;

use work.phys.all;

architecture rtl of t is
begin
  -- A physical value is a count of its type's step unit, and dividing two values of one
  -- physical type gives an integer, as it does for the predefined `time'
  q_step <= C_1MHZ / 1 Hz;
  -- A generic default is a physical literal too, and the generic is an integer here
  q_gen_k <= CLOCK_FREQ / 1 kHz;
  q_gen_hz <= CLOCK_FREQ / 1 Hz;
  q_baud <= SYMBOL_RATE / 1 kBd;
  -- Adding two values of one physical type keeps the type, so the sum needs no unit
  q_sum <= C_SUM / 1 Hz;
  -- The divisor's unit is the same type's, only a different multiple of the step
  q_div <= C_1MHZ / 2 kHz;
  q_frac <= C_FRAC / 1 Hz;
  -- The MEMORY type counts bytes, and its steps are 1024 rather than 1000
  q_kib <= C_1MIB / 1 KiB;
  q_mib <= C_1MIB / 1 Byte;
  q_mib3 <= C_3MIB / 1 Byte;
  q_mbd <= C_1MBD / 1 Bd;
end architecture rtl;
