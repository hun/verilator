-- DESCRIPTION: Verilator: a process loop range that reads a signal runs at run time
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

package pkg is
  function pick(v : integer) return integer;
end package pkg;

package body pkg is
  function pick(v : integer) return integer is
  begin
    return v + 100;
  end function pick;
end package body pkg;

library ieee;
use ieee.std_logic_1164.all;

library work;
use work.pkg.all;

entity t is
  port (
    n : in  integer;
    o : out integer
  );
end entity t;

architecture rtl of t is
begin
  proc : process (n)
    variable acc : integer := 0;
  begin
    -- A sequential loop range is evaluated when the loop is reached, so it may read n
    for i in 1 to pick(n) - 100 loop
      acc := acc + i;
    end loop;
    o <= acc;
  end process proc;
end architecture rtl;
