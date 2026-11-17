-- DESCRIPTION: Verilator: VHDL predefined array comparison and numeric_std comparison
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t is
  port (
    o : out std_logic_vector(9 downto 0);
    u : out std_logic_vector(2 downto 0)
  );
end;

architecture rtl of t is
  type bits_t is array (natural range <>) of std_logic;
  constant a : std_logic_vector(1 downto 0) := "01";
  constant b : std_logic_vector(0 downto 0) := "1";
  constant c : std_logic_vector(1 downto 0) := "10";
  constant ua : unsigned(1 downto 0) := "01";
  constant ub : unsigned(0 downto 0) := "1";
  constant sa : signed(1 downto 0) := "11";
  constant sb : signed(1 downto 0) := "01";
  constant ta : bits_t(1 downto 0) := "01";
  constant tb : bits_t(0 downto 0) := "1";
  constant tc : bits_t(1 downto 0) := "10";
begin
  -- Predefined arrays: equality needs equal lengths, ordering is lexicographic
  o(0) <= '1' when a = b else '0';
  o(1) <= '1' when a < b else '0';
  o(2) <= '1' when c < b else '0';
  o(3) <= '1' when b = a else '0';
  o(4) <= '1' when a <= b else '0';
  o(5) <= '1' when c > b else '0';
  -- numeric_std unsigned resizes
  o(6) <= '1' when ua = ub else '0';
  o(7) <= '1' when ua < ub else '0';
  -- numeric_std signed compares numerically
  o(8) <= '1' when sa < sb else '0';
  o(9) <= '1' when sa = signed'("01") else '0';
  -- A user-declared one-bit-element array uses the same array rules
  u(0) <= '1' when ta = tb else '0';
  u(1) <= '1' when ta < tb else '0';
  u(2) <= '1' when tc > tb else '0';
end;
