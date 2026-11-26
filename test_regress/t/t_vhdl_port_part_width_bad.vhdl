-- DESCRIPTION: Verilator: VHDL input port part associated with an actual of another length
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0
entity sub is
  port (p : in bit_vector(7 downto 0); y : out bit_vector(3 downto 0));
end entity sub;

architecture rtl of sub is
begin
  y <= p(3 downto 0);
end architecture rtl;

entity t is
  port (b8 : in bit_vector(7 downto 0); y : out bit_vector(3 downto 0));
end entity t;

architecture rtl of t is
begin
  u : entity work.sub port map (p(3 downto 0) => b8, p(7 downto 4) => "0000", y => y);
end architecture rtl;
