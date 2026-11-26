-- DESCRIPTION: Verilator: a for loop over an object name rather than a range is refused
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A loop parameter range must be a discrete range (IEEE 1076-2008 10.10).
library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (
    a, s : in bit;
    y : out bit_vector(0 to 3)
  );
end entity t;

architecture rtl of t is

begin
  process (a)
    variable vv : bit_vector(0 to 3);
  begin
    for i in vv loop
      null;
    end loop;
    y(0) <= a;
  end process;
end architecture rtl;
