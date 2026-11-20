-- DESCRIPTION: Verilator: VHDL null-array assignment is a no-op
--
-- This file ONLY is placed in the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

-- A null range declares a legal zero-length array (IEEE 1076-2008 5.3.2).
-- Assigning an aggregate to it writes no elements; the frontend must not
-- emit a zero-width assignment, which V3Width rejects.

library ieee;
use ieee.std_logic_1164.all;

entity t_vhdl_null_assign is
  port (
    y : out std_logic
  );
end entity;

architecture rtl of t_vhdl_null_assign is
  signal z : std_logic_vector(-1 downto 0);
  signal n : integer := z'length;
begin
  z <= (others => '0');
  y <= '1' when n = 0 else '0';
end architecture;
