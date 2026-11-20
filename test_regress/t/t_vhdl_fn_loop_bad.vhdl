-- DESCRIPTION: Verilator: a function loop that never terminates statically
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

package p is
  function runon_for return natural;
  function runon_while return natural;
end package p;

package body p is
  -- A range that is never exhausted, since the body cannot leave the loop
  function runon_for return natural is
    variable v : natural := 0;
  begin
    for i in 0 to natural'high loop
      v := v + 1;
    end loop;
    return v;
  end function runon_for;

  -- natural is never negative, so the condition holds for every value
  function runon_while return natural is
    variable v : natural := 0;
  begin
    while v >= 0 loop
      v := v + 1;
    end loop;
    return v;
  end function runon_while;
end package body p;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.p.all;

entity t is
  port (
    o  : out std_logic_vector(7 downto 0);
    o2 : out std_logic_vector(7 downto 0)
  );
end entity t;

architecture rtl of t is
begin
  o <= std_logic_vector(to_unsigned(runon_for, 8));
  o2 <= std_logic_vector(to_unsigned(runon_while, 8));
end architecture rtl;
