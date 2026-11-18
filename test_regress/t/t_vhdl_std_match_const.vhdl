-- DESCRIPTION: Verilator: VHDL a don't-care in a constant is a wildcard
--
-- This file ONLY is placed in the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t is
  port (
    x  : in  std_logic_vector(2 downto 0);
    m1 : out std_logic;
    m2 : out std_logic
  );
end;

architecture rtl of t is
  -- The don't-care is in a constant, not in a literal: STD_MATCH and the
  -- matching equality still treat it as a wildcard (IEEE 1076-2008 16.8.3,
  -- 9.2.3). GHDL: x = "110" and x = "100" both match, "111" and "010" do not.
  constant pat : std_logic_vector(2 downto 0) := "1-0";
begin
  m1 <= '1' when std_match(x, pat) else '0';
  m2 <= '1' when (x ?= pat) else '0';
end;
