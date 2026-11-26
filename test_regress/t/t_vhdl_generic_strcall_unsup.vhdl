-- DESCRIPTION: Verilator: VHDL string generic actual that is not a literal or outer generic is unsupported
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

entity sub is generic (S : string := "x"); port (o : out bit); end entity sub;
architecture rtl of sub is begin o <= '1'; end architecture rtl;
entity t is port (o : out bit); end entity t;
architecture rtl of t is
  function f return string is begin return "ab"; end function;
begin
  u : entity work.sub generic map (S => f) port map (o => o);
end architecture rtl;
