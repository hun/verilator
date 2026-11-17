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
    u : out std_logic_vector(2 downto 0);
    w : out std_logic_vector(7 downto 0)
  );
end;

architecture rtl of t is
  type bits_t is array (natural range <>) of std_logic;
  type intarr_t is array (natural range <>) of integer;
  type vecarr_t is array (natural range <>) of std_logic_vector(1 downto 0);
  function fa return intarr_t(0 to 1) is
  begin
    return (1, -1);
  end function;
  function fb return intarr_t(0 to 1) is
  begin
    return (1, 1);
  end function;
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
  constant ia : intarr_t(0 to 1) := (1, -1);
  constant ib : intarr_t(0 to 1) := (1, 1);
  constant ic : intarr_t(0 to 1) := (0, 9);
  constant id : intarr_t(0 to 2) := (1, -1, 2);
  constant va : vecarr_t(0 to 1) := ("01", "10");
  constant vb : vecarr_t(0 to 1) := ("01", "11");
  constant vc : vecarr_t(0 to 2) := ("01", "10", "00");
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
  -- Arrays of signed integers compare element-wise, with a length tie-break
  w(0) <= '1' when ia = ib else '0';
  w(1) <= '1' when ia < ib else '0';
  w(2) <= '1' when ic > ib else '0';
  -- Arrays of vectors only have equality, not ordering
  w(3) <= '1' when va = vb else '0';
  w(4) <= '1' when va = vc else '0';
  w(5) <= '1' when va = va else '0';
  w(6) <= '1' when id > ia else '0';
  w(7) <= '1' when fa < fb else '0';
end;
