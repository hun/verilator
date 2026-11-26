-- DESCRIPTION: Verilator: VHDL output port part associated with an actual of another length
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0
entity sub is
  port (a : in bit_vector(3 downto 0); q : out bit_vector(7 downto 0));
end entity sub;

architecture rtl of sub is
begin
  q <= a & a;
end architecture rtl;

entity t is
  port (a : in bit_vector(3 downto 0); o8 : out bit_vector(7 downto 0));
end entity t;

architecture rtl of t is
  signal s : bit_vector(3 downto 0);
begin
  u : entity work.sub port map (a => a, q(3 downto 0) => o8, q(7 downto 4) => s);
end architecture rtl;
