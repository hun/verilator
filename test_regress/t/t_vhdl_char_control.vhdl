-- DESCRIPTION: Verilator: a control character is a literal of the character type
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    sel : in  std_logic;
    q   : out std_logic
  );
end entity t;

architecture rtl of t is
  function pick(c : boolean; a : character; b : character) return character is
  begin
    if c then
      return a;
    end if;
    return b;
  end function pick;
  constant ch_c : character := pick(true, NUL, 'a');
begin
  q <= '1' when ch_c = NUL and sel = '1' else '0';
end architecture rtl;
