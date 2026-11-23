-- DESCRIPTION: Verilator: VHDL positional then named port and generic associations
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

entity child is
  generic (
    w : integer := 4;
    k : integer := 1
  );
  port (
    p1 : in  bit_vector(3 downto 0);
    p2 : in  bit_vector(3 downto 0);
    y  : out bit_vector(3 downto 0)
  );
end;

architecture rtl of child is
begin
  y <= (p1 and p2) when k = 1 else (p1 or p2);
end;

entity t is
  port (
    a  : in  bit_vector(3 downto 0);
    b  : in  bit_vector(3 downto 0);
    y1 : out bit_vector(3 downto 0);
    y2 : out bit_vector(3 downto 0)
  );
end;

architecture rtl of t is
begin
  -- The positional associations come first and the named ones follow
  u1 : entity work.child generic map (4, k => 1) port map (a, p2 => b, y => y1);
  u2 : entity work.child generic map (4, k => 2) port map (a, b, y => y2);
end;
