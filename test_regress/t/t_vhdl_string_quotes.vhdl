-- DESCRIPTION: Verilator: a doubled quote inside a string literal
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

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
  constant Trailing_c : string := "a""";
  constant Whole_c : string := "got """ & "X" & """";
begin
  len <= Quoted_c'length;
  -- The character between the quotes of Quoted_c is the quote itself
  inner <= character'pos(Quoted_c(3)) + character'pos('"') + Trailing_c'length;
end architecture;
