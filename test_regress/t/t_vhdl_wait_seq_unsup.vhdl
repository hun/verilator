-- DESCRIPTION: Verilator: a wait with nothing to wait on is refused in a multi-wait process
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
    y : out std_logic
  );
end;

architecture rtl of t is
  constant c : std_logic := '1';
begin
  proc : process
  begin
    wait for 1 ns;
    wait until c = '1';
    wait on c;
    y <= '1';
  end process;
end;
