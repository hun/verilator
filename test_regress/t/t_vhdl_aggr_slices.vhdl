-- DESCRIPTION: Verilator: a positional aggregate whose elements are slices
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
    i : in  std_logic_vector(31 downto 0);
    o : out std_logic_vector(31 downto 0);
    m : out std_logic_vector(7 downto 0)
  );
end entity t;

architecture rtl of t is
begin
  -- Each slice is a run of elements of the aggregate's own type; a scalar may be mixed in
  o <= (i(23 downto 16), i(15 downto 8), i(7 downto 0), i(31 downto 24));
  m <= (i(3 downto 0), i(7), i(6), i(5), i(4));
end architecture rtl;
