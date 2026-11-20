-- DESCRIPTION: Verilator: VHDL resolved logic signals accept several drivers
--
-- This file ONLY is placed in the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

-- std_logic carries a resolution function, and so does every composite built
-- from it (IEEE 1164). Two drivers on a scalar, a record member and a custom
-- array element are legal; only std_ulogic is unresolved. Both drivers below
-- hold '1', so the two-state model has a defined value.

library ieee;
use ieee.std_logic_1164.all;

entity t_vhdl_logic_resolved is
  port (
    y : out std_logic
  );
end entity;

architecture rtl of t_vhdl_logic_resolved is
  type rec_t is record
    f : std_logic;
  end record;
  type arr_t is array (0 to 0) of std_logic;
  signal s : std_logic;
  signal r : rec_t;
  signal a : arr_t;
begin
  s <= '1';
  s <= '1';
  r.f <= '1';
  r.f <= '1';
  a(0) <= '1';
  a(0) <= '1';
  y <= s and r.f and a(0);
end architecture;
