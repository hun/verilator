-- DESCRIPTION: Verilator: a character function result does not retype an unsigned operand
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity t is
  port (
    x : in  std_logic_vector(7 downto 0);
    y : out std_logic_vector(8 downto 0)
  );
end entity t;

architecture rtl of t is
  function same_chr(c : character) return character is
  begin
    return c;
  end function same_chr;

  -- Inlining same_chr leaves an eight-bit character result behind, which has the same
  -- data type as an eight-bit unsigned value
  function copy(s : string) return string is
    variable r : string(s'range);
  begin
    for i in s'range loop
      r(i) := same_chr(s(i));
    end loop;
    return r;
  end function copy;
  constant text_c : string := copy("abc");
begin
  process (all)
    variable s : unsigned(8 downto 0);
  begin
    s := ("0" & unsigned(x)) + 1;
    y <= std_logic_vector(s);
  end process;
end architecture rtl;
