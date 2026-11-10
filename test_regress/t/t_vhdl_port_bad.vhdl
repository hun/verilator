-- DESCRIPTION: Verilator: VHDL ports that cannot be modelled
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

-- An unconstrained port on the selected top has no width
entity top_in is
  port (
    i : in std_logic_vector
  );
end;

architecture rtl of top_in is
begin
end;
