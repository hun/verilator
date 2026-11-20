-- DESCRIPTION: Verilator: VHDL two-state inout port diagnostic
--
-- This file ONLY is placed in the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

-- VHDL permits a `bit` (or `bit_vector`) inout port, but V3Tristate requires
-- a four-state net. The frontend must refuse it at the port, with a VHDL
-- diagnostic, rather than let the middle end reject the net type.

entity t is
  port (
    q : inout bit
  );
end entity;

architecture rtl of t is
begin
end architecture;
