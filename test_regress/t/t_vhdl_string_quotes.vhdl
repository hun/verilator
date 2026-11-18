-- DESCRIPTION: Verilator: a doubled quote inside a string literal
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

-- A doubled quote inside a literal is one quote character (IEEE 1076-2008 15.7), so a
-- message can quote a value: open-logic's rate limiter reports
-- "Mode_g must be either ""SMOOTH"" or ""BLOCK""".

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    len : out natural;
    inner : out natural
  );
end entity;

architecture rtl of t is
  constant Quoted_c : string := "a ""b"" c";
  constant Whole_c : string := "got """ & "X" & """";
begin
  len <= Quoted_c'length;
  -- The character between the quotes of Quoted_c is the quote itself
  inner <= character'pos(Quoted_c(3));
end architecture;
