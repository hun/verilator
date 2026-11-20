-- DESCRIPTION: Verilator: an array local written under a run-time condition
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- An inlined function's array local becomes a real variable, whose writes are
-- statements. A condition that does not fold would lower both paths into the same
-- statement list, so the write is refused rather than run on both paths.

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package p is
  function f(v : unsigned; c : std_logic) return unsigned;
end package;

package body p is
  function f(v : unsigned; c : std_logic) return unsigned is
    type arr_t is array (natural range <>) of unsigned(7 downto 0);
    variable a : arr_t(0 to 1) := (others => (others => '0'));
  begin
    a(0) := resize(v, 8);
    if c = '1' then
      a(1)(3) := '1';
    end if;
    return a(0) or a(1);
  end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.p.all;

entity t is
  port (v : in unsigned(7 downto 0); c : in std_logic; o : out unsigned(7 downto 0));
end entity;

architecture rtl of t is
begin
  o <= f(v, c);
end architecture;
