-- DESCRIPTION: Verilator: a for loop over the range of a record field
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;

entity t is
  port (d : in std_logic_vector(3 downto 0);
        q : out std_logic_vector(3 downto 0));
end entity t;

architecture rtl of t is
  type rec_t is record
    bits : std_logic_vector(3 downto 0);
  end record;
  signal r : rec_t;
begin
  process (d)
  begin
    for i in r.bits'range loop
      r.bits(i) <= d(3 - i);
    end loop;
  end process;
  q <= r.bits;
end architecture rtl;
