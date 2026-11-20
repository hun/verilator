-- DESCRIPTION: Verilator: a call in a constant whose actual is not static is refused
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package p is
  function width_of(v : integer) return integer;
end package p;

package body p is
  function width_of(v : integer) return integer is
    variable i : integer := 0;
  begin
    while 2 ** i < v loop
      i := i + 1;
    end loop;
    return i;
  end function width_of;
end package body p;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.p.all;

entity t is
  port (
    ctrl : in  unsigned(7 downto 0);
    o    : out integer
  );
end entity t;

architecture rtl of t is
  -- The initializer reads a signal, so the constant has no value while the fold runs
  constant d_c : integer := width_of(to_integer(ctrl));
begin
  o <= d_c;
end architecture rtl;
